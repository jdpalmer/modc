/*
 * CLI build/link: compile graph, package cache, foreign objs, link.
 *
 * Compilation pipeline: [lex → pp → parse → type check → emit → QBE]
 */
#include "cli.h"
#include <time.h>

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
	uint64_t key;
	char hex[24];
	char objpath[HOST_PATH_MAX];
	char ifacepath[HOST_PATH_MAX];
	int hit;
} BuildPkg;

// Lex and preprocess one path into c->tokens; apply -D from j onward.
static int
lex_pp_file(Compiler* c, const char* path, int j) {
	char* text;

	c->tokens = NULL;
	c->tokens_len = 0;
	c->tokens_cap = 0;
	c->pos = 0;
	pp_clear_macros(c);
	pp_clear_once(c);
	pp_init(c);
	for (; j < c->cli_defs_len; j++)
		pp_define(c, c->cli_defs[j]);
	free(c->infile);
	c->infile = xstrdup(path);
	text = read_file(path, NULL);
	if (text == NULL) {
		fprintf(stderr, "modc: cannot read %s: %s\n", path, strerror(errno));
		return 1;
	}
	lex_file(c, c->infile, text, 1);
	free(text);
	if (c->error_count)
		return 1;
	pp_run(c);
	return c->error_count ? 1 : 0;
}

// Wall-ish seconds from clock() for MODC_PROFILE timings.
static double
now_sec(void) {
	return (double)clock() / (double)CLOCKS_PER_SEC;
}

/* Lex/parse/typecheck an already-discovered file list. Does not emit or free files. */
static int
compile_graph(Compiler* c, char** files, int nfiles) {
	int i, r;
	TuCache* cache;
	int profile;
	double t0, t1, t_lexpp, t_prescan, t_parse, t_type;

	type_init(c);
	c->unit_files = NULL;
	c->unit_files_len = 0;
	c->unit_src_start = c->src_files_len;
	r = 0;
	profile = getenv("MODC_PROFILE") != NULL;
	t_lexpp = t_prescan = t_parse = t_type = 0;
	cache = xmalloc((size_t)nfiles * sizeof(*cache));
	memset(cache, 0, (size_t)nfiles * sizeof(*cache));
	for (i = 0; i < nfiles; i++) {
		if (c->unit_files_len % 8 == 0)
			c->unit_files = xrealloc(c->unit_files,
						(c->unit_files_len + 8) * sizeof(char*));
		c->unit_files[c->unit_files_len++] = xstrdup(files[i]);
	}
	for (i = 0; i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		if (lex_pp_file(c, files[i], 0)) {
			r = 1;
			break;
		}
		if (profile) {
			t1 = now_sec();
			t_lexpp += t1 - t0;
		}
		cache[i].tokens = c->tokens;
		cache[i].tokens_len = c->tokens_len;
		cache[i].tokens_cap = c->tokens_cap;
		cache[i].infile = c->infile; /* span.file aliases this */
		/* Detach so the next lex_pp_file does not drop or free these. */
		c->tokens = NULL;
		c->tokens_len = 0;
		c->tokens_cap = 0;
		c->infile = NULL;
		if (c->error_count) {
			r = 1;
			break;
		}
	}
	/* Package-wide: type-name stubs, type bodies, layouts, then func/method
	 * signatures — Go-style package scope so file basename order does not matter. */
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->infile = cache[i].infile;
		c->tokens = cache[i].tokens;
		c->tokens_len = cache[i].tokens_len;
		c->tokens_cap = cache[i].tokens_cap;
		c->pos = 0;
		prescan_unit_type_names(c);
		if (profile) {
			t1 = now_sec();
			t_prescan += t1 - t0;
		}
		c->tokens = NULL;
		c->tokens_len = 0;
		c->tokens_cap = 0;
		c->infile = NULL;
		if (c->error_count) {
			r = 1;
			break;
		}
	}
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->infile = cache[i].infile;
		c->tokens = cache[i].tokens;
		c->tokens_len = cache[i].tokens_len;
		c->tokens_cap = cache[i].tokens_cap;
		c->pos = 0;
		prescan_unit_type_bodies(c);
		if (profile) {
			t1 = now_sec();
			t_prescan += t1 - t0;
		}
		c->tokens = NULL;
		c->tokens_len = 0;
		c->tokens_cap = 0;
		c->infile = NULL;
		if (c->error_count) {
			r = 1;
			break;
		}
	}
	if (r == 0)
		type_layout_pending(c);
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->infile = cache[i].infile;
		c->tokens = cache[i].tokens;
		c->tokens_len = cache[i].tokens_len;
		c->tokens_cap = cache[i].tokens_cap;
		c->pos = 0;
		prescan_unit_funcs(c);
		if (profile) {
			t1 = now_sec();
			t_prescan += t1 - t0;
		}
		c->tokens = NULL;
		c->tokens_len = 0;
		c->tokens_cap = 0;
		c->infile = NULL;
		if (c->error_count) {
			r = 1;
			break;
		}
	}
	for (i = 0; r == 0 && i < nfiles; i++) {
		if (profile)
			t0 = now_sec();
		c->infile = cache[i].infile;
		c->tokens = cache[i].tokens;
		c->tokens_len = cache[i].tokens_len;
		c->tokens_cap = cache[i].tokens_cap;
		c->pos = 0;
		parse_unit(c);
		if (profile) {
			t1 = now_sec();
			t_parse += t1 - t0;
		}
		c->tokens = NULL;
		c->tokens_len = 0;
		c->tokens_cap = 0;
		if (c->error_count) {
			r = 1;
			break;
		}
	}
	if (r) {
		if (profile)
			fprintf(stderr,
				"modc profile: lex+pp=%.3fs prescan=%.3fs parse=%.3fs (failed)\n",
				t_lexpp, t_prescan, t_parse);
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
	if (c->error_count) {
		if (profile)
			fprintf(stderr,
				"modc profile: lex+pp=%.3fs prescan=%.3fs parse=%.3fs type=%.3fs\n",
				t_lexpp, t_prescan, t_parse, t_type);
		free(cache);
		return 1;
	}
	if (profile)
		fprintf(stderr,
			"modc profile: lex+pp=%.3fs prescan=%.3fs parse=%.3fs type=%.3fs\n",
			t_lexpp, t_prescan, t_parse, t_type);
	/* Keep cache[i].infile alive: AST/Symbol spans still alias those strings. */
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
	r = compile_graph(c, files, nfiles);
	for (i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);
	if (r)
		return 1;
	if (!c->check_only) {
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
		if (c->error_count)
			return 1;
	}
	return 0;
}

// Clear TU state so the next root can reuse the same Compiler.
void
reset_comp_state(Compiler* c) {
	c->tokens = NULL;
	c->tokens_len = 0;
	c->tokens_cap = 0;
	c->pos = 0;
	c->error_count = 0;
	c->fatal = 0;
	c->symbols = NULL;
	free(c->symbol_tab);
	c->symbol_tab = NULL;
	c->symbol_tab_cap = 0;
	c->symbols_len = 0;
	c->block = 0;
	c->funcs = NULL;
	c->funcs_len = 0;
	c->funcs_cap = 0;
	c->globals = NULL;
	c->globals_len = 0;
	c->globals_cap = 0;
	c->current_fn = NULL;
	c->type_list = NULL;
	c->unit_files = NULL;
	c->unit_files_len = 0;
	type_init(c);
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

// Host C compiler for assemble/link (MODC_CC, CC, or platform default).
static const char*
tool_cc(void) {
	const char* cc;

	cc = getenv("MODC_CC");
	if (cc && cc[0])
		return cc;
	cc = getenv("CC");
	if (cc && cc[0])
		return cc;
#ifdef _WIN32
	return "clang";
#else
	return "cc";
#endif
}

// Path to the qbe binary (MODC_QBE or qbe/qbe.exe).
static const char*
tool_qbe(void) {
	const char* q;

	q = getenv("MODC_QBE");
	if (q && q[0])
		return q;
#ifdef _WIN32
	if (host_is_file("qbe.exe"))
		return "qbe.exe";
	if (host_is_file("./qbe.exe"))
		return "./qbe.exe";
#endif
	return "qbe";
}

// QBE -t triple for this host (or MODC_QBE_TARGET).
static const char*
tool_qbe_target(void) {
	const char* t;

	t = getenv("MODC_QBE_TARGET");
	if (t && t[0])
		return t;
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

	cxx = getenv("MODC_CXX");
	if (cxx && cxx[0])
		return cxx;
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

	for (i = 0; i < c->csources_len; i++)
		if (src_is_cxx(c->csources[i]))
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

	for (i = 0; i < c->incpaths_len; i++)
		if (add_arg(argv, argc, "-I") || add_arg(argv, argc, c->incpaths[i]))
			return 1;
	if (!c->no_system_includes) {
		for (i = 0; i < c->sysincpaths_len; i++)
			if (add_arg(argv, argc, "-I") ||
			    add_arg(argv, argc, c->sysincpaths[i]))
				return 1;
	}
	for (i = 0; i < c->cli_defs_len; i++)
		if (add_arg(argv, argc, "-D") || add_arg(argv, argc, c->cli_defs[i]))
			return 1;
	for (i = 0; i < c->framework_paths_len; i++)
		if (add_arg(argv, argc, "-F") ||
		    add_arg(argv, argc, c->framework_paths[i]))
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

// Fingerprint toolchain, target, and user -D/-I for package cache keys.
static uint64_t
hash_compile_knobs(Compiler* c, const char* projroot) {
	uint64_t h;
	char key[HOST_PATH_MAX];
	int i;

	h = cache_hash_str(MODC_VERSION);
	h = cache_hash_mix(h, cache_hash_str(tool_qbe_target()));
	h = cache_hash_mix(h, cache_hash_str(cache_host_os()));
	h = cache_hash_mix(h, cache_hash_str(tool_cc()));
	h = cache_hash_mix(h, cache_hash_str(tool_cxx()));
	for (i = 0; i < c->cli_defs_len; i++)
		h = cache_hash_mix(h, cache_hash_str(c->cli_defs[i]));
	for (i = 0; i < c->incpaths_len; i++) {
		cache_path_key(c->incpaths[i], projroot, key, sizeof(key));
		h = cache_hash_mix(h, cache_hash_str(key));
	}
	h = cache_hash_mix(h, c->no_system_includes ? 1 : 0);
	return h;
}

// Content-address key for one c_sources object (arch-aware).
static uint64_t
foreign_obj_key(Compiler* c, const char* src, const char* comp, const char* projroot) {
	uint64_t h;
	char key[HOST_PATH_MAX];
	int i;

	h = cache_hash_file(src);
	h = cache_hash_mix(h, cache_hash_str(comp));
	h = cache_hash_mix(h, cache_hash_str(tool_qbe_target()));
	h = cache_hash_mix(h, cache_hash_str(cache_host_os()));
	for (i = 0; i < c->cli_defs_len; i++)
		h = cache_hash_mix(h, cache_hash_str(c->cli_defs[i]));
	for (i = 0; i < c->incpaths_len; i++) {
		cache_path_key(c->incpaths[i], projroot, key, sizeof(key));
		h = cache_hash_mix(h, cache_hash_str(key));
	}
	h = cache_hash_mix(h, c->no_system_includes ? 1 : 0);
	if (src_is_objc(src))
		h = cache_hash_mix(h, cache_hash_str("-fobjc-arc"));
#ifdef _WIN32
	h = cache_hash_mix(h, cache_hash_str("_CRT_SECURE_NO_WARNINGS"));
#endif
	return h;
}

// Compile or reuse cached objects for #pragma modc c_sources.
static int
compile_foreign_sources(Compiler* c, CliOpts* o, const char* entry, const char* dir,
			char objs[][512], int* nobj) {
	const char* argv[MaxCmdArgs];
	char srcdir[1024], crooot[HOST_PATH_MAX], cached[HOST_PATH_MAX];
	char depfile[HOST_PATH_MAX], depmeta[HOST_PATH_MAX];
	char hex[32], what[HOST_PATH_MAX], projroot[HOST_PATH_MAX];
	int i, argc;
	uint64_t key;

	*nobj = 0;
	crooot[0] = 0;
	projroot[0] = 0;
	cache_root_for(entry ? entry : ".", crooot, sizeof(crooot));
	cache_project_root(entry ? entry : ".", projroot, sizeof(projroot));
	for (i = 0; i < c->csources_len; i++) {
		const char* comp;

		if (!host_is_file(c->csources[i])) {
			fprintf(stderr, "modc: c_sources file not found: %s\n",
				c->csources[i]);
			return 1;
		}
		if (*nobj >= MaxForeignObj) {
			fprintf(stderr, "modc: too many c_sources files\n");
			return 1;
		}
		comp = src_is_cxx(c->csources[i]) ? tool_cxx() : tool_cc();
		snprintf(objs[*nobj], 512, "%s/foreign%d.o", dir, *nobj);
		key = foreign_obj_key(c, c->csources[i], comp, projroot);
		cache_hash_hex(key, hex, sizeof(hex));
		snprintf(cached, sizeof(cached), "%s/foreign/%s.o", crooot, hex);
		snprintf(depmeta, sizeof(depmeta), "%s.deps", cached);
		snprintf(depfile, sizeof(depfile), "%s/foreign%d.d", dir, *nobj);
		snprintf(what, sizeof(what), "foreign %s", c->csources[i]);
		if (host_is_file(cached) && cache_deps_valid(depmeta)) {
			if (cache_copy_file(cached, objs[*nobj]) != 0)
				return 1;
			cache_log(o->verbose, "hit", what);
			(*nobj)++;
			continue;
		}
		cache_log(o->verbose, "miss", what);
		argc = 0;
		if (add_arg(argv, &argc, comp) || add_arg(argv, &argc, "-c"))
			goto toolong;
#ifdef _WIN32
		if (add_arg(argv, &argc, "-D_CRT_SECURE_NO_WARNINGS"))
			goto toolong;
#endif
		if (add_compile_flags(c, argv, &argc) ||
		    add_arg(argv, &argc, "-MMD") || add_arg(argv, &argc, "-MF") ||
		    add_arg(argv, &argc, depfile))
			goto toolong;
		src_dirname(c->csources[i], srcdir, sizeof(srcdir));
		if (add_arg(argv, &argc, "-I") || add_arg(argv, &argc, srcdir))
			goto toolong;
		if (src_is_objc(c->csources[i]) &&
		    add_arg(argv, &argc, "-fobjc-arc"))
			goto toolong;
		if (add_arg(argv, &argc, c->csources[i]) ||
		    add_arg(argv, &argc, "-o") || add_arg(argv, &argc, objs[*nobj]))
			goto toolong;
		if (run_argv(o->verbose, argv) != 0)
			return 1;
		if (cache_copy_file(objs[*nobj], cached) == 0 &&
		    cache_depfile_to_deps(depfile, depmeta) != 0)
			(void)host_unlink(cached);
		(*nobj)++;
	}
	return 0;

toolong:
	fprintf(stderr, "modc: too many compile arguments\n");
	return 1;
}

// Link package objs + foreign objs + libs into outpath.
static int
link_modc_objs(Compiler* c, CliOpts* o, const char* outpath, char objs[][512], int nobjs,
	       char foreign[][512], int nforeign) {
	const char* argv[MaxCmdArgs];
	const char* linker;
	int i, argc, n;

	linker = needs_cxx_link(c) ? tool_cxx() : tool_cc();
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
	if (!c->no_system_includes) {
		for (i = 0; i < c->syslibpaths_len; i++)
			if (add_arg(argv, &argc, "-L") ||
			    add_arg(argv, &argc, c->syslibpaths[i]))
				goto toolong;
	}
	for (i = 0; i < c->c_libs_len; i++)
		if (add_arg(argv, &argc, "-l") || add_arg(argv, &argc, c->c_libs[i]))
			goto toolong;
	for (i = 0; i < c->frameworks_len; i++)
		if (add_arg(argv, &argc, "-framework") ||
		    add_arg(argv, &argc, c->frameworks[i]))
			goto toolong;
	for (i = 0; i < o->linkargv_len; i++)
		if (add_arg(argv, &argc, o->linkargv[i]))
			goto toolong;
	return run_argv(o->verbose, argv);

toolong:
	fprintf(stderr, "modc: too many linker arguments\n");
	return 1;
}

// Sanitize package directory basename for cache path segments.
static void
pkg_id_from_dir(const char* dir, char* out, size_t out_len) {
	const char* base;
	size_t i, j;

	base = host_path_last_sep(dir);
	base = base ? base + 1 : dir;
	if (base[0] == 0)
		base = "root";
	for (i = 0, j = 0; base[i] && j + 1 < out_len; i++) {
		char ch = base[i];
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
			out[j++] = ch;
		else
			out[j++] = '_';
	}
	out[j] = 0;
}

// Unique absolute package roots from the discovered .mc file list.
static int
collect_build_pkgs(char** files, int nfiles, BuildPkg* pkgs, int* npkgs) {
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
		pkg_id_from_dir(root, pkgs[n].id, sizeof(pkgs[n].id));
		n++;
	}
	*npkgs = n;
	return 0;
}

// Mix immediate *.h files under dir into an impl hash.
static uint64_t
hash_pkg_headers(const char* dir, uint64_t h) {
	HostDir* d;
	const char* name;
	char path[HOST_PATH_MAX];
	size_t n;

	d = host_opendir(dir);
	if (d == NULL)
		return h;
	while ((name = host_readdir(d)) != NULL) {
		if (name[0] == '.')
			continue;
		n = strlen(name);
		if (n < 3 || strcmp(name + n - 2, ".h") != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, name);
		if (host_is_file(path))
			h = cache_hash_mix(h, cache_hash_file(path));
	}
	host_closedir(d);
	return h;
}

// Content hash of a package implementation (.mc + local .h).
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
		h = cache_hash_mix(h, cache_hash_file(files[i]));
	}
	h = hash_pkg_headers(pkg->dir, h);
	return h;
}

// Save headers opened while preprocessing the current ModC graph.
static int
save_modc_deps(Compiler* c, const char* path) {
	char** deps;
	int i, j, n;

	deps = xmalloc((size_t)(c->src_files_len - c->unit_src_start) * sizeof(char*));
	n = 0;
	for (i = c->unit_src_start; i < c->src_files_len; i++) {
		for (j = 0; j < c->unit_files_len; j++)
			if (strcmp(c->src_files[i], c->unit_files[j]) == 0)
				break;
		if (j == c->unit_files_len)
			deps[n++] = c->src_files[i];
	}
	i = cache_write_deps(path, deps, n);
	free(deps);
	return i;
}

// Compute cache paths and hit flags from implementation hashes.
static void
pkg_fill_keys(BuildPkg* pkgs, int npkgs, uint64_t knobs, const char* crooot) {
	int i, j;
	uint64_t h, needsh;
	char depmeta[HOST_PATH_MAX], needspath[HOST_PATH_MAX], stored[256];
	char hex[24];

	for (i = 0; i < npkgs; i++) {
		h = cache_hash_mix(pkgs[i].impl, knobs);
		pkgs[i].key = h;
		cache_hash_hex(h, pkgs[i].hex, sizeof(pkgs[i].hex));
		snprintf(pkgs[i].objpath, sizeof(pkgs[i].objpath), "%s/pkg/%s-%s/pkg.o",
			 crooot, pkgs[i].id, pkgs[i].hex);
		snprintf(pkgs[i].ifacepath, sizeof(pkgs[i].ifacepath),
			 "%s/pkg/%s-%s/iface", crooot, pkgs[i].id, pkgs[i].hex);
		pkgs[i].hit = 0;
		snprintf(depmeta, sizeof(depmeta), "%s.deps", pkgs[i].objpath);
		if (!host_is_file(pkgs[i].objpath) || !host_is_file(pkgs[i].ifacepath) ||
		    !cache_deps_valid(depmeta))
			continue;
		snprintf(needspath, sizeof(needspath), "%s/pkg/%s-%s/needs", crooot,
			 pkgs[i].id, pkgs[i].hex);
		needsh = 0;
		for (j = 0; j < npkgs; j++) {
			if (i == j)
				continue;
			needsh = cache_hash_mix(needsh, pkgs[j].impl);
		}
		cache_hash_hex(needsh, hex, sizeof(hex));
		stored[0] = 0;
		if (cache_read_str(needspath, stored, sizeof(stored)) != 0)
			continue;
		if (strcmp(stored, hex) != 0)
			continue;
		pkgs[i].hit = 1;
	}
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

// Persist c_libs/frameworks/csources for a later all-hit link.
static int
save_link_meta(Compiler* c, CliOpts* o, const char* path) {
	char buf[65536];
	int off, i;

	off = 0;
	for (i = 0; i < c->c_libs_len && off < (int)sizeof(buf) - 8; i++)
		off += snprintf(buf + off, sizeof(buf) - (size_t)off, "L %s\n",
				c->c_libs[i]);
	for (i = 0; i < c->frameworks_len && off < (int)sizeof(buf) - 8; i++)
		off += snprintf(buf + off, sizeof(buf) - (size_t)off, "F %s\n",
				c->frameworks[i]);
	for (i = 0; i < c->csources_len && off < (int)sizeof(buf) - 8; i++)
		off += snprintf(buf + off, sizeof(buf) - (size_t)off, "S %s\n",
				c->csources[i]);
	for (i = 0; i < o->linkargv_len && off < (int)sizeof(buf) - 8; i++)
		off += snprintf(buf + off, sizeof(buf) - (size_t)off, "X %s\n",
				o->linkargv[i]);
	if (off < 0 || off >= (int)sizeof(buf))
		return 1;
	return cache_write_bytes(path, buf, (size_t)off);
}

// Record an absolute c_sources path (linkmeta reload).
static void
add_csource_abs(Compiler* c, const char* path) {
	int i;

	if (path == NULL || path[0] == 0)
		return;
	for (i = 0; i < c->csources_len; i++)
		if (strcmp(c->csources[i], path) == 0)
			return;
	if (c->csources_len % 8 == 0)
		c->csources = xrealloc(c->csources, (c->csources_len + 8) * sizeof(char*));
	c->csources[c->csources_len++] = xstrdup(path);
}

// Restore link pragmas from a cached linkmeta file.
static int
load_link_meta(Compiler* c, const char* path) {
	char* text;
	size_t n, i, start;
	char line[HOST_PATH_MAX + 4];

	text = read_file(path, &n);
	if (text == NULL)
		return 1;
	c->c_libs_len = 0;
	c->frameworks_len = 0;
	c->csources_len = 0;
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
			add_csource_abs(c, line + 2);
	}
	free(text);
	return 0;
}

// Emit one package to QBE/asm/.o and store it in the cache.
static int
emit_pkg_object(Compiler* c, CliOpts* o, BuildPkg* pkg, int pkg_index, const char* dir,
		const char* crooot) {
	const char* argv[8];
	char qbe[512], asmpath[512], obj[512], strsym[128];
	FILE* f;

	(void)crooot;
	snprintf(qbe, sizeof(qbe), "%s/pkg%d.qbe", dir, pkg_index);
	snprintf(asmpath, sizeof(asmpath), "%s/pkg%d.s", dir, pkg_index);
	snprintf(obj, sizeof(obj), "%s/pkg%d.o", dir, pkg_index);
	snprintf(strsym, sizeof(strsym), "__string_%s_%.8s", pkg->id, pkg->hex);
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
	if (c->error_count)
		return 1;
	argv[0] = tool_qbe();
	argv[1] = "-t";
	argv[2] = tool_qbe_target();
	argv[3] = "-o";
	argv[4] = asmpath;
	argv[5] = qbe;
	argv[6] = NULL;
	if (run_argv(o->verbose, argv) != 0)
		return 1;
	argv[0] = tool_cc();
	argv[1] = "-c";
	argv[2] = asmpath;
	argv[3] = "-o";
	argv[4] = obj;
	argv[5] = NULL;
	if (run_argv(o->verbose, argv) != 0)
		return 1;
	if (cache_copy_file(obj, pkg->objpath) != 0)
		return 1;
	pkg->hit = 1;
	return 0;
}

/* Compile path → cached package objs → foreign objs → link into outpath.
 * dir is an existing temp directory; caller cleans artifacts. */
int
compile_link_exe(Compiler* c, CliOpts* o, const char* path, const char* dir, const char* outpath) {
	char** files;
	int nfiles, npkgs, i, j, all_hit, r;
	BuildPkg pkgs[MaxCachePkgs];
	uint64_t knobs, ghash;
	char crooot[HOST_PATH_MAX], projroot[HOST_PATH_MAX], ghex[24], meta[HOST_PATH_MAX],
		stamp[HOST_PATH_MAX];
	char stamphex[24], stored[64], what[HOST_PATH_MAX];
	char foreign[MaxForeignObj][512];
	char objs[MaxCachePkgs][512];
	int nforeign;
	uint64_t linkh;

	files = NULL;
	nfiles = 0;
	if (pkg_discover(c, path, &files, &nfiles))
		return 1;
	if (collect_build_pkgs(files, nfiles, pkgs, &npkgs) != 0) {
		fprintf(stderr, "modc: too many packages in build graph\n");
		for (i = 0; i < nfiles; i++)
			free(files[i]);
		free(files);
		return 1;
	}
	cache_root_for(path, crooot, sizeof(crooot));
	cache_project_root(path, projroot, sizeof(projroot));
	knobs = hash_compile_knobs(c, projroot);
	for (i = 0; i < npkgs; i++)
		pkgs[i].impl = pkg_impl_hash(&pkgs[i], files, nfiles, projroot);
	pkg_fill_keys(pkgs, npkgs, knobs, crooot);
	ghash = graph_digest(pkgs, npkgs, knobs);
	cache_hash_hex(ghash, ghex, sizeof(ghex));
	snprintf(meta, sizeof(meta), "%s/prog/%s/linkmeta", crooot, ghex);
	snprintf(stamp, sizeof(stamp), "%s/prog/%s/stamp", crooot, ghex);

	all_hit = 1;
	for (i = 0; i < npkgs; i++) {
		if (!pkgs[i].hit)
			all_hit = 0;
	}

	if (all_hit && host_is_file(meta) && load_link_meta(c, meta) == 0) {
		int foreign_ready;

		for (i = 0; i < npkgs; i++) {
			snprintf(what, sizeof(what), "pkg %s", pkgs[i].id);
			cache_log(o->verbose, "hit", what);
			snprintf(objs[i], sizeof(objs[i]), "%s", pkgs[i].objpath);
		}
		cache_log(o->verbose, "hit", "graph");
		linkh = cache_hash_str(outpath);
		for (i = 0; i < npkgs; i++)
			linkh = cache_hash_mix(linkh, pkgs[i].key);
		for (i = 0; i < c->csources_len; i++)
			linkh = cache_hash_mix(
				linkh, foreign_obj_key(c, c->csources[i],
						       src_is_cxx(c->csources[i])
							       ? tool_cxx()
							       : tool_cc(),
						       projroot));
		for (i = 0; i < o->linkargv_len; i++)
			linkh = cache_hash_mix(linkh, cache_hash_str(o->linkargv[i]));
		cache_hash_hex(linkh, stamphex, sizeof(stamphex));
		foreign_ready = 1;
		for (i = 0; i < c->csources_len; i++) {
			char cached[HOST_PATH_MAX], depmeta[HOST_PATH_MAX], hex[32];
			uint64_t key;

			key = foreign_obj_key(c, c->csources[i],
					      src_is_cxx(c->csources[i]) ? tool_cxx()
									: tool_cc(),
					      projroot);
			cache_hash_hex(key, hex, sizeof(hex));
			snprintf(cached, sizeof(cached), "%s/foreign/%s.o", crooot, hex);
			snprintf(depmeta, sizeof(depmeta), "%s.deps", cached);
			if (!host_is_file(cached) || !cache_deps_valid(depmeta))
				foreign_ready = 0;
			else {
				snprintf(what, sizeof(what), "foreign %s", c->csources[i]);
				cache_log(o->verbose, "hit", what);
			}
		}
		stored[0] = 0;
		if (foreign_ready && host_is_file(outpath) &&
		    cache_read_str(stamp, stored, sizeof(stored)) == 0 &&
		    strcmp(stored, stamphex) == 0) {
			for (i = 0; i < nfiles; i++)
				free(files[i]);
			free(files);
			return 0;
		}
		if (compile_foreign_sources(c, o, path, dir, foreign, &nforeign) != 0) {
			for (i = 0; i < nfiles; i++)
				free(files[i]);
			free(files);
			return 1;
		}
		r = link_modc_objs(c, o, outpath, objs, npkgs, foreign, nforeign);
		if (r == 0)
			(void)cache_write_str(stamp, stamphex);
		for (i = 0; i < nfiles; i++)
			free(files[i]);
		free(files);
		return r;
	}

	cache_log(o->verbose, "miss", "graph");
	r = compile_graph(c, files, nfiles);
	for (i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);
	files = NULL;
	if (r != 0)
		return 1;

	/* Any dependency source change conservatively invalidates this object. */
	{
		char ifacehex[MaxCachePkgs][24];
		char curiface[HOST_PATH_MAX], depmeta[HOST_PATH_MAX];
		char needspath[HOST_PATH_MAX], needshex[24], stored[64];
		uint64_t needs;

		for (i = 0; i < npkgs; i++) {
			cache_hash_hex(pkgs[i].impl, ifacehex[i], sizeof(ifacehex[i]));
			snprintf(curiface, sizeof(curiface), "%s/pkg/%s/iface", crooot,
				 pkgs[i].id);
			if (cache_write_str(curiface, ifacehex[i]) != 0)
				return 1;
			if (cache_write_str(pkgs[i].ifacepath, ifacehex[i]) != 0)
				return 1;
		}
		for (i = 0; i < npkgs; i++) {
			needs = 0;
			for (j = 0; j < npkgs; j++) {
				if (j == i)
					continue;
				needs = cache_hash_mix(needs, pkgs[j].impl);
			}
			cache_hash_hex(needs, needshex, sizeof(needshex));
			snprintf(needspath, sizeof(needspath), "%s/pkg/%s-%s/needs", crooot,
				 pkgs[i].id, pkgs[i].hex);
			snprintf(depmeta, sizeof(depmeta), "%s.deps", pkgs[i].objpath);
			pkgs[i].hit = 0;
			if (host_is_file(pkgs[i].objpath) &&
			    cache_deps_valid(depmeta) &&
			    cache_read_str(needspath, stored, sizeof(stored)) == 0 &&
			    strcmp(stored, needshex) == 0)
				pkgs[i].hit = 1;
		}
		for (i = 0; i < npkgs; i++) {
			snprintf(what, sizeof(what), "pkg %s", pkgs[i].id);
			if (pkgs[i].hit) {
				cache_log(o->verbose, "hit", what);
				snprintf(objs[i], sizeof(objs[i]), "%s", pkgs[i].objpath);
				continue;
			}
			cache_log(o->verbose, "miss", what);
			if (emit_pkg_object(c, o, &pkgs[i], i, dir, crooot) != 0)
				return 1;
			snprintf(objs[i], sizeof(objs[i]), "%s", pkgs[i].objpath);
		}
		for (i = 0; i < npkgs; i++) {
			snprintf(depmeta, sizeof(depmeta), "%s.deps", pkgs[i].objpath);
			if (save_modc_deps(c, depmeta) != 0)
				return 1;
		}
		/* Stamp needs beside every obj for the next run. */
		for (i = 0; i < npkgs; i++) {
			needs = 0;
			for (j = 0; j < npkgs; j++) {
				if (j == i)
					continue;
				needs = cache_hash_mix(needs, pkgs[j].impl);
			}
			cache_hash_hex(needs, needshex, sizeof(needshex));
			snprintf(needspath, sizeof(needspath), "%s/pkg/%s-%s/needs", crooot,
				 pkgs[i].id, pkgs[i].hex);
			(void)cache_write_str(needspath, needshex);
		}
	}
	if (save_link_meta(c, o, meta) != 0)
		return 1;

	if (compile_foreign_sources(c, o, path, dir, foreign, &nforeign) != 0)
		return 1;
	r = link_modc_objs(c, o, outpath, objs, npkgs, foreign, nforeign);
	if (r == 0) {
		linkh = cache_hash_str(outpath);
		for (i = 0; i < npkgs; i++)
			linkh = cache_hash_mix(linkh, pkgs[i].key);
		for (i = 0; i < c->csources_len; i++)
			linkh = cache_hash_mix(
				linkh, foreign_obj_key(c, c->csources[i],
						       src_is_cxx(c->csources[i])
							       ? tool_cxx()
							       : tool_cc(),
						       projroot));
		for (i = 0; i < o->linkargv_len; i++)
			linkh = cache_hash_mix(linkh, cache_hash_str(o->linkargv[i]));
		cache_hash_hex(linkh, stamphex, sizeof(stamphex));
		(void)cache_write_str(stamp, stamphex);
	}
	return r;
}

/* Heap string: executable name from a file or directory path. */
char*
default_out_name(const char* path) {
	char buf[PATH_MAX], cwd[PATH_MAX];
	const char *base, *dot;
	size_t n;
	char* name;

	if (path == NULL || path[0] == 0)
		path = ".";
	if (strcmp(path, ".") == 0 || strcmp(path, "./") == 0 || strcmp(path, ".\\") == 0) {
		if (host_getcwd(cwd, sizeof(cwd)) != 0)
#ifdef _WIN32
			return xstrdup("a.exe");
#else
			return xstrdup("a.out");
#endif
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
#ifdef _WIN32
		return xstrdup("a.exe");
#else
		return xstrdup("a.out");
#endif
	dot = strrchr(base, '.');
	if (dot && strcmp(dot, ".mc") == 0 && dot > base)
		name = xstrndup(base, (size_t)(dot - base));
	else
		name = xstrdup(base);
#ifdef _WIN32
	n = strlen(name);
	if (n < 4 || strcmp(name + n - 4, ".exe") != 0) {
		char* with;

		with = xmalloc(n + 5);
		memcpy(with, name, n);
		memcpy(with + n, ".exe", 5);
		free(name);
		return with;
	}
#endif
	return name;
}

// Default build input to "." when none was given.
void
ensure_build_root(CliOpts* o) {
	if (o->files_len == 0)
		add_file(o, ".");
}

// Unlink temp artifacts and remove the temp directory.
void
cleanup_tmpdir(const char* dir, const char* a, const char* b, const char* c) {
	if (a)
		host_unlink(a);
	if (b)
		host_unlink(b);
	if (c)
		host_unlink(c);
	host_rmdir(dir);
}

