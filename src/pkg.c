/*
 * Packages: import discovery, search paths, host link hints.
 *
 * Compilation pipeline: lex → pp → parse → type check → emit → QBE
 * (feeds the driver before lex: which .mc files form the Compiler.)
 *
 * #pragma modc c_libs / frameworks / c_sources; pkg_discover walks a package root;
 * the driver stitches those TUs into one Compiler.
 */
#include "ast.h"
#include "host_os.h"
#include <ctype.h>

enum { MaxPkgFiles = 256,
       MaxImportDepth = 64 };

// Register a directory searched when resolving import "spec".
void pkg_add_search_path(Compiler* c, const char* dir) {
	if (dir == NULL || dir[0] == 0)
		return;
	if (c->pkgpaths_len % 8 == 0)
		c->pkgpaths = xrealloc(c->pkgpaths, (c->pkgpaths_len + 8) * sizeof(char*));
	c->pkgpaths[c->pkgpaths_len++] = xstrdup(dir);
}

// Record a -l library name for the final host link line (deduped).
void pkg_add_clib(Compiler* c, const char* lib) {
	int i;

	if (lib == NULL || lib[0] == 0)
		return;
	for (i = 0; i < c->c_libs_len; i++)
		if (strcmp(c->c_libs[i], lib) == 0)
			return;
	if (c->c_libs_len % 8 == 0)
		c->c_libs = xrealloc(c->c_libs, (c->c_libs_len + 8) * sizeof(char*));
	c->c_libs[c->c_libs_len++] = xstrdup(lib);
}

// Record a macOS framework for the final host link line (deduped).
void pkg_add_framework(Compiler* c, const char* name) {
	int i;

	if (name == NULL || name[0] == 0)
		return;
	for (i = 0; i < c->frameworks_len; i++)
		if (strcmp(c->frameworks[i], name) == 0)
			return;
	if (c->frameworks_len % 8 == 0)
		c->frameworks = xrealloc(c->frameworks,
					 (c->frameworks_len + 8) * sizeof(char*));
	c->frameworks[c->frameworks_len++] = xstrdup(name);
}

// True for C/C++/ObjC source extensions accepted by c_sources pragmas.
static int
has_csource_ext(const char* path) {
	size_t n;

	if (path == NULL)
		return 0;
	n = strlen(path);
	if (n >= 2 && strcmp(path + n - 2, ".c") == 0)
		return 1;
	if (n >= 2 && strcmp(path + n - 2, ".m") == 0)
		return 1;
	if (n >= 4 && strcmp(path + n - 4, ".cpp") == 0)
		return 1;
	if (n >= 4 && strcmp(path + n - 4, ".cxx") == 0)
		return 1;
	if (n >= 3 && strcmp(path + n - 3, ".mm") == 0)
		return 1;
	return 0;
}

// Register a directory searched for #include <Framework/...> on macOS.
void comp_add_framework_path(Compiler* c, const char* dir) {
	int i;

	if (dir == NULL || dir[0] == 0)
		return;
	for (i = 0; i < c->framework_paths_len; i++)
		if (strcmp(c->framework_paths[i], dir) == 0)
			return;
	if (c->framework_paths_len % 8 == 0)
		c->framework_paths = xrealloc(c->framework_paths, (c->framework_paths_len + 8) * sizeof(char*));
	c->framework_paths[c->framework_paths_len++] = xstrdup(dir);
}

// True when path exists and is a directory.
static int
is_dir(const char* path) {
	return host_is_dir(path);
}

// True when path exists and is a regular file.
static int
is_file(const char* path) {
	return host_is_file(path);
}

// Copy the parent directory of a file path into out.
static void
dirname_copy(const char* path, char* out, size_t n) {
	host_dirname(path, out, n);
}

// Resolve and record an extra C/C++ TU from a #pragma modc c_sources path.
void pkg_add_csource(Compiler* c, const char* from_file, const char* relpath) {
	char pkgdir[1024], path[1024];
	char* joined;
	int i;

	if (relpath == NULL || relpath[0] == 0 || from_file == NULL)
		return;
	if (!has_csource_ext(relpath))
		return;
	dirname_copy(from_file, pkgdir, sizeof(pkgdir));
	joined = host_join_path(pkgdir, relpath);
	snprintf(path, sizeof(path), "%s", joined);
	free(joined);
	for (i = 0; i < c->csources_len; i++)
		if (strcmp(c->csources[i], path) == 0)
			return;
	if (c->csources_len % 8 == 0)
		c->csources = xrealloc(c->csources, (c->csources_len + 8) * sizeof(char*));
	c->csources[c->csources_len++] = xstrdup(path);
}

// True when a filename ends in .mc.
static int
has_src_ext(const char* name) {
	size_t n;

	if (name == NULL)
		return 0;
	n = strlen(name);
	return n > 3 && strcmp(name + n - 3, ".mc") == 0;
}

// True for *_test.mc files excluded from normal package discovery.
int pkg_is_test_src(const char* path) {
	const char* base;
	size_t n;

	if (path == NULL)
		return 0;
	base = host_path_basename(path);
	n = strlen(base);
	/* at least "x_test.mc" */
	return n >= 9 && strcmp(base + n - 8, "_test.mc") == 0;
}

// True if dir has an immediate subdirectory that itself contains .mc sources
// (a nested package). Mixed trees like test/ use this to avoid treating the
// parent as one giant package.
static int
dir_has_pkg_subdir(const char* dir) {
	HostDir* d;
	const char* name;
	char path[HOST_PATH_MAX], child[HOST_PATH_MAX];
	HostDir* sub;
	const char* sn;
	size_t n;

	d = host_opendir(dir);
	if (d == NULL)
		return 0;
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
			n = strlen(sn);
			if (n >= 3 && strcmp(sn + n - 3, ".mc") == 0) {
				snprintf(child, sizeof(child), "%s/%s", path, sn);
				if (host_is_file(child)) {
					host_closedir(sub);
					host_closedir(d);
					return 1;
				}
			}
		}
		host_closedir(sub);
	}
	host_closedir(d);
	return 0;
}

// Package root for a .mc file.
// Directory packages (mod.mc present, or multi-file dir without nested pkg
// subdirs) use the directory. Single-file packages living beside other packages
// (e.g. test/pkg_log.mc next to test/pkg_math/) use the file path itself so
// package-private static does not leak across unrelated .mc files.
void pkg_file_root(const char* mcfile, char* out, size_t out_len) {
	char dir[HOST_PATH_MAX], mod[HOST_PATH_MAX];
	HostDir* d;
	const char* name;
	int nmc;
	size_t n;

	if (out_len == 0)
		return;
	out[0] = 0;
	if (mcfile == NULL || mcfile[0] == 0)
		return;
	dirname_copy(mcfile, dir, sizeof(dir));
	snprintf(mod, sizeof(mod), "%s/mod.mc", dir);
	if (host_is_file(mod)) {
		snprintf(out, out_len, "%s", dir);
		return;
	}
	nmc = 0;
	d = host_opendir(dir);
	if (d) {
		while ((name = host_readdir(d)) != NULL) {
			if (name[0] == '.')
				continue;
			n = strlen(name);
			if (n < 3 || strcmp(name + n - 3, ".mc") != 0)
				continue;
			nmc++;
			if (nmc > 1)
				break;
		}
		host_closedir(d);
	}
	if (nmc > 1 && dir_has_pkg_subdir(dir)) {
		snprintf(out, out_len, "%s", mcfile);
		return;
	}
	if (nmc == 1 && dir_has_pkg_subdir(dir)) {
		snprintf(out, out_len, "%s", mcfile);
		return;
	}
	snprintf(out, out_len, "%s", dir);
}

// Package prefix for method linker names (basename of pkg dir, lowercased).
void pkg_mangle_from_file(const char* mcfile, char* out, size_t out_len) {
	char root[1024];
	const char *base, *dot;
	size_t i, n;

	out[0] = 0;
	if (mcfile == NULL || out_len == 0)
		return;
	pkg_file_root(mcfile, root, sizeof(root));
	base = host_path_basename(root);
	if (base[0] == 0 || strcmp(base, ".") == 0)
		base = host_path_basename(mcfile);
	dot = strrchr(base, '.');
	if (dot && strcmp(dot, ".mc") == 0)
		n = (size_t)(dot - base);
	else
		n = strlen(base);
	if (n >= out_len)
		n = out_len - 1;
	for (i = 0; i < n; i++)
		out[i] = (char)tolower((unsigned char)base[i]);
	out[i] = 0;
}

// Try spec as a package dir or as spec.mc under base.
static int
try_pkg_at(const char* base, const char* spec, char* out, size_t out_len) {
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", base, spec);
	if (is_dir(path)) {
		snprintf(out, out_len, "%s", path);
		return 1;
	}
	snprintf(path, sizeof(path), "%s/%s.mc", base, spec);
	if (is_file(path)) {
		snprintf(out, out_len, "%s", path);
		return 1;
	}
	return 0;
}

// Search -M paths, MODC_PATH, then modc_pkg for spec.
static int
try_pkg_search(Compiler* c, const char* spec, char* out, size_t out_len) {
	const char *env, *p, *q;
	char* pathdup;
	char sep;
	int i;

	for (i = 0; i < c->pkgpaths_len; i++)
		if (try_pkg_at(c->pkgpaths[i], spec, out, out_len))
			return 1;
	env = getenv("MODC_PATH");
	if (env && env[0]) {
		sep = host_path_list_sep();
		pathdup = xstrdup(env);
		for (p = pathdup; *p;) {
			q = strchr(p, sep);
			if (q)
				*(char*)q = 0;
			if (p[0] && try_pkg_at(p, spec, out, out_len)) {
				free(pathdup);
				return 1;
			}
			if (q == NULL)
				break;
			p = q + 1;
		}
		free(pathdup);
	}
	if (c->modc_pkg && try_pkg_at(c->modc_pkg, spec, out, out_len))
		return 1;
	return 0;
}

// Resolve import "spec" relative to from_file, the project root, vendor/,
// -M, MODC_PATH, then modc_pkg.
static int
resolve_import(Compiler* c, const char* spec, const char* from_file, char* out, size_t out_len) {
	char basedir[1024], cur[1024], vendor[1024];

	dirname_copy(from_file, basedir, sizeof(basedir));
	if (try_pkg_at(basedir, spec, out, out_len))
		return 0;
	if (c->project_root[0] && try_pkg_at(c->project_root, spec, out, out_len))
		return 0;

	snprintf(cur, sizeof(cur), "%s", basedir);
	for (;;) {
		snprintf(vendor, sizeof(vendor), "%s/vendor", cur);
		if (is_dir(vendor) && try_pkg_at(vendor, spec, out, out_len))
			return 0;
		if (strcmp(cur, "/") == 0 || strcmp(cur, ".") == 0)
			break;
		dirname_copy(cur, vendor, sizeof(vendor));
		if (strcmp(vendor, cur) == 0)
			break;
		snprintf(cur, sizeof(cur), "%s", vendor);
	}

	if (try_pkg_search(c, spec, out, out_len))
		return 0;

	error_at(c, (Span){from_file, 1, 1, 1},
		 "cannot find package \"%s\"", spec);
	return 1;
}

// Sort package sources with mod.mc first, then alphabetically by basename.
static int
cmp_src(const void* a, const void* b) {
	const char* sa = *(const char* const*)a;
	const char* sb = *(const char* const*)b;
	const char *na, *nb;
	int moda, modb;

	na = host_path_basename(sa);
	nb = host_path_basename(sb);
	moda = strcmp(na, "mod.mc") == 0;
	modb = strcmp(nb, "mod.mc") == 0;
	if (moda != modb)
		return moda ? -1 : 1;
	return strcmp(na, nb);
}

// List .mc files in a package directory; want() filters basenames (NULL = all .mc).
// Single-file roots: accepted when want is NULL or want(root) is true.
// empty_ok: directory with zero matches returns success (for pkg_list_tests).
static int
list_mc_files(const char* root, int (*want)(const char*), int empty_ok, char*** out, int* out_len) {
	HostDir* d;
	const char* name;
	char path[1024];
	char** files;
	int n, cap;

	*out = NULL;
	*out_len = 0;
	if (is_file(root)) {
		if (want && !want(root))
			return 1;
		*out = xmalloc(sizeof(char*));
		(*out)[0] = xstrdup(root);
		*out_len = 1;
		return 0;
	}
	if (!is_dir(root))
		return 1;
	d = host_opendir(root);
	if (d == NULL)
		return 1;
	files = NULL;
	n = 0;
	cap = 0;
	while ((name = host_readdir(d)) != NULL) {
		if (name[0] == '.')
			continue;
		if (!has_src_ext(name))
			continue;
		if (want && !want(name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", root, name);
		if (!is_file(path))
			continue;
		if (n >= cap) {
			cap = cap ? cap * 2 : 8;
			files = xrealloc(files, cap * sizeof(char*));
		}
		files[n++] = xstrdup(path);
	}
	host_closedir(d);
	if (n > 1)
		qsort(files, (size_t)n, sizeof(char*), cmp_src);
	*out = files;
	*out_len = n;
	return (n == 0 && !empty_ok) ? 1 : 0;
}

// Non-test sources: keep .mc that are not *_test.mc.
static int
want_pkg_src(const char* name) {
	return !pkg_is_test_src(name);
}

// List non-test .mc sources in a package directory (or return a single file).
static int
list_sources(const char* root, char*** out, int* out_len) {
	/* Single-file roots are accepted as-is (including *_test.mc). */
	if (is_file(root))
		return list_mc_files(root, NULL, 0, out, out_len);
	return list_mc_files(root, want_pkg_src, 0, out, out_len);
}

// Lex+preprocess one file and collect top-level import strings (discovery only).
static int
scan_imports(Compiler* c, const char* path, char*** imps, int* nimps) {
	char* text;
	Tok* save_toks;
	int save_ntok, save_tokcap, save_pos, save_nerror;
	char* save_infile;
	int i, depth, n, cap;
	char** list;

	*imps = NULL;
	*nimps = 0;
	text = read_file(path, NULL);
	if (text == NULL) {
		fprintf(stderr, "modc: cannot read %s\n", path);
		return 1;
	}
	/* Fast path: no "import" spelling → no package imports (avoid full PP). */
	if (strstr(text, "import") == NULL) {
		free(text);
		return 0;
	}
	save_toks = c->tokens;
	save_ntok = c->tokens_len;
	save_tokcap = c->tokens_cap;
	save_pos = c->pos;
	save_nerror = c->error_count;
	save_infile = c->infile;
	c->tokens = NULL;
	c->tokens_len = 0;
	c->tokens_cap = 0;
	c->pos = 0;
	c->infile = xstrdup(path);
	/* Lex only — do not preprocess. `import` is a keyword; pulling #include
	 * trees here previously doubled windows.h work for no benefit. */
	lex_file(c, c->infile, text, 1);
	free(text);
	if (c->error_count) {
		c->tokens = save_toks;
		c->tokens_len = save_ntok;
		c->tokens_cap = save_tokcap;
		c->pos = save_pos;
		c->error_count = save_nerror;
		c->infile = save_infile;
		return 1;
	}
	list = NULL;
	n = 0;
	cap = 0;
	depth = 0;
	for (i = 0; i < c->tokens_len; i++) {
		Tok* t = &c->tokens[i];
		if (t->kind == TkPunct && t->punct == PnLbrace)
			depth++;
		else if (t->kind == TkPunct && t->punct == PnRbrace)
			depth--;
		if (depth != 0)
			continue;
		if (t->kind == TkKw && t->kw == KwImport) {
			if (i + 2 < c->tokens_len && c->tokens[i + 1].kind == TkString) {
				if (n >= cap) {
					cap = cap ? cap * 2 : 4;
					list = xrealloc(list, cap * sizeof(char*));
				}
				list[n++] = xstrdup(c->tokens[i + 1].s);
			}
		}
	}
	c->tokens = save_toks;
	c->tokens_len = save_ntok;
	c->tokens_cap = save_tokcap;
	c->pos = save_pos;
	c->error_count = save_nerror;
	c->infile = save_infile;
	*imps = list;
	*nimps = n;
	return 0;
}

typedef struct {
	char* files[MaxPkgFiles];
	int files_len;
	char* stack[MaxImportDepth];
	int stack_len;
} Disc;

// True when a source path is already in the discovery closure.
static int
disc_has(Disc* d, const char* path) {
	int i;

	for (i = 0; i < d->files_len; i++)
		if (strcmp(d->files[i], path) == 0)
			return 1;
	return 0;
}

// True when path is on the active import stack (cycle detection).
static int
disc_on_stack(Disc* d, const char* path) {
	int i;

	for (i = 0; i < d->stack_len; i++)
		if (strcmp(d->stack[i], path) == 0)
			return 1;
	return 0;
}

// DFS a file and its imports, building the transitive source closure.
static int
visit_file(Compiler* c, Disc* d, const char* path) {
	char **imps, **srcs;
	int nimps, nsrcs, i, j;
	char root[1024];

	if (disc_has(d, path))
		return 0;
	if (disc_on_stack(d, path)) {
		error_at(c, (Span){path, 1, 1, 1}, "import cycle involving %s", path);
		return 1;
	}
	if (d->stack_len >= MaxImportDepth) {
		error_at(c, (Span){path, 1, 1, 1}, "import nesting too deep");
		return 1;
	}
	d->stack[d->stack_len++] = (char*)path;
	imps = NULL;
	nimps = 0;
	if (scan_imports(c, path, &imps, &nimps)) {
		d->stack_len--;
		return 1;
	}
	for (i = 0; i < nimps; i++) {
		if (resolve_import(c, imps[i], path, root, sizeof(root))) {
			for (j = 0; j < nimps; j++)
				free(imps[j]);
			free(imps);
			d->stack_len--;
			return 1;
		}
		srcs = NULL;
		nsrcs = 0;
		if (list_sources(root, &srcs, &nsrcs)) {
			error_at(c, (Span){path, 1, 1, 1},
				 "package \"%s\" has no .mc sources", imps[i]);
			for (j = 0; j < nimps; j++)
				free(imps[j]);
			free(imps);
			d->stack_len--;
			return 1;
		}
		for (j = 0; j < nsrcs; j++) {
			if (visit_file(c, d, srcs[j])) {
				int k;
				for (k = 0; k < nsrcs; k++)
					free(srcs[k]);
				free(srcs);
				for (k = 0; k < nimps; k++)
					free(imps[k]);
				free(imps);
				d->stack_len--;
				return 1;
			}
		}
		for (j = 0; j < nsrcs; j++)
			free(srcs[j]);
		free(srcs);
		free(imps[i]);
		imps[i] = NULL;
	}
	free(imps);
	d->stack_len--;
	if (d->files_len >= MaxPkgFiles) {
		error_at(c, (Span){path, 1, 1, 1}, "too many package source files");
		return 1;
	}
	if (!disc_has(d, path))
		d->files[d->files_len++] = xstrdup(path);
	return 0;
}

// Resolve a package spec from cwd, -M, MODC_PATH, then modc_pkg (no importer context).
int pkg_resolve_spec(Compiler* c, const char* spec, char* out, size_t out_len) {
	if (spec == NULL || spec[0] == 0)
		return 1;
	if (try_pkg_at(".", spec, out, out_len))
		return 0;
	return try_pkg_search(c, spec, out, out_len) ? 0 : 1;
}

// Discover all .mc sources reachable from a package root via imports.
int pkg_discover(Compiler* c, const char* root, char*** out_files, int* out_n) {
	Disc d;
	char** srcs;
	int nsrcs, i;

	memset(&d, 0, sizeof(d));
	c->project_root[0] = 0;
	cache_project_root(root, c->project_root, sizeof(c->project_root));
	srcs = NULL;
	nsrcs = 0;
	if (list_sources(root, &srcs, &nsrcs)) {
		error_at(c, (Span){root, 1, 1, 1},
			 "no .mc sources at \"%s\"", root);
		return 1;
	}
	for (i = 0; i < nsrcs; i++) {
		if (visit_file(c, &d, srcs[i])) {
			int j;
			for (j = 0; j < nsrcs; j++)
				free(srcs[j]);
			free(srcs);
			for (j = 0; j < d.files_len; j++)
				free(d.files[j]);
			return 1;
		}
	}
	for (i = 0; i < nsrcs; i++)
		free(srcs[i]);
	free(srcs);
	*out_files = xmalloc((size_t)d.files_len * sizeof(char*));
	for (i = 0; i < d.files_len; i++)
		(*out_files)[i] = d.files[i];
	*out_n = d.files_len;
	return 0;
}

// List *_test.mc files in a package directory for test driver use.
int pkg_list_tests(const char* root, char*** out, int* out_len) {
	return list_mc_files(root, pkg_is_test_src, 1, out, out_len);
}
