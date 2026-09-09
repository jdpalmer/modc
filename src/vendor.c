/*
 * Package vendoring: modc.ini → modc.lock → vendor/
 *
 * Resolves git dependencies, dedupes by import name, and materializes a flat
 * vendor/ tree for the existing import resolver.
 */
#include "ast.h"
#include "host_os.h"

enum { MaxIniSect = 128,
       MaxIniKey = 32,
       MaxLocked = 256,
       MaxQueue = 256 };

typedef struct {
	char* key;
	char* val;
} IniKV;

typedef struct {
	char* name;
	IniKV kv[MaxIniKey];
	int nkv;
} IniSect;

typedef struct {
	IniSect sect[MaxIniSect];
	int nsect;
} IniFile;

typedef struct {
	char* name;
	char* git;
	char* tag;
	char* rev;
	char* branch;
	char* subdir;
} DepSpec;

typedef struct {
	char* name;
	char* git;
	char* rev;
	char* subdir;
} LockedPkg;

typedef struct {
	char root[4096];   /* app root (absolute) */
	char vendor[4096]; /* root/vendor */
	int verbose;
	int check_only;
} VendorCtx;

static int rm_rf(const char* path);
static int copy_tree(const char* src, const char* dst);
static int write_stamp(const char* pkgdir, const char* rev);
static int materialize_cloned(VendorCtx* ctx, LockedPkg* p, const char* srcdir);

// Release all heap storage owned by a parsed INI file.
static void
ini_free(IniFile* ini) {
	int i, j;

	for (i = 0; i < ini->nsect; i++) {
		free(ini->sect[i].name);
		for (j = 0; j < ini->sect[i].nkv; j++) {
			free(ini->sect[i].kv[j].key);
			free(ini->sect[i].kv[j].val);
		}
	}
	ini->nsect = 0;
}

// Trim leading and trailing whitespace in place; returns the new start of the string.
static char*
trim(char* s) {
	char* e;

	while (*s && isspace((unsigned char)*s))
		s++;
	if (*s == 0)
		return s;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		e--;
	*e = 0;
	return s;
}

// Return the value for key in section s, or NULL if the key is missing.
static const char*
ini_get(IniSect* s, const char* key) {
	int i;

	if (s == NULL)
		return NULL;
	for (i = 0; i < s->nkv; i++)
		if (s->kv[i].key && strcmp(s->kv[i].key, key) == 0)
			return s->kv[i].val;
	return NULL;
}

// Append a heap-copied key/value pair to a section; fails when the section is full.
static int
ini_add_kv(IniSect* s, const char* key, const char* val) {
	if (s->nkv >= MaxIniKey)
		return 1;
	s->kv[s->nkv].key = xstrdup(key);
	s->kv[s->nkv].val = xstrdup(val);
	s->nkv++;
	return 0;
}

// Create a new empty section in the INI file; returns NULL when the file is full.
static IniSect*
ini_add_sect(IniFile* ini, const char* name) {
	IniSect* s;

	if (ini->nsect >= MaxIniSect)
		return NULL;
	s = &ini->sect[ini->nsect++];
	s->name = xstrdup(name);
	s->nkv = 0;
	return s;
}

// Parse modc.ini/modc.lock text into sections and key/value pairs.
static int
ini_parse(IniFile* ini, const char* text, const char* path) {
	char *copy, *p, *line, *eq, *sectname;
	IniSect* cur;

	(void)path;
	memset(ini, 0, sizeof(*ini));
	copy = xstrdup(text);
	cur = NULL;
	for (p = copy; *p;) {
		line = p;
		p = strchr(p, '\n');
		if (p) {
			*p = 0;
			p++;
		}
		line = trim(line);
		if (line[0] == 0 || line[0] == '#' || line[0] == ';')
			continue;
		if (line[0] == '[') {
			eq = strchr(line, ']');
			if (eq == NULL) {
				free(copy);
				return 1;
			}
			*eq = 0;
			sectname = trim(line + 1);
			cur = ini_add_sect(ini, sectname);
			if (cur == NULL) {
				free(copy);
				return 1;
			}
			continue;
		}
		if (cur == NULL)
			continue;
		eq = strchr(line, '=');
		if (eq == NULL)
			continue;
		*eq = 0;
		if (ini_add_kv(cur, trim(line), trim(eq + 1)))
			return 1;
	}
	free(copy);
	return 0;
}

// Read an INI file from disk and parse it into ini.
static int
ini_load(IniFile* ini, const char* path) {
	char* text;
	int err;

	text = read_file(path, NULL);
	if (text == NULL)
		return 1;
	err = ini_parse(ini, text, path);
	free(text);
	return err;
}

// True when path exists and is a directory.
static int
is_dir_path(const char* path) {
	return host_is_dir(path);
}

// Run a shell command and return its exit status, or -1 on spawn/wait failure.
static int
run_status(const char* cmd) {
	return host_run(cmd);
}

// Run a command and capture the first line of stdout (used for git ls-remote hashes).
static int
run_capture(char* out, size_t out_len, const char* cmd) {
	return host_run_capture(out, out_len, cmd);
}

// Fill a dependency spec from a [deps.NAME] section; requires git and exactly one pin.
static int
dep_from_sect(DepSpec* d, IniSect* s) {
	const char *git, *tag, *rev, *branch, *subdir;
	int npin;

	git = ini_get(s, "git");
	if (git == NULL || git[0] == 0)
		return 1;
	d->git = xstrdup(git);
	tag = ini_get(s, "tag");
	rev = ini_get(s, "rev");
	branch = ini_get(s, "branch");
	subdir = ini_get(s, "subdir");
	npin = (tag && tag[0]) + (rev && rev[0]) + (branch && branch[0]);
	if (npin != 1)
		return 2;
	if (tag && tag[0])
		d->tag = xstrdup(tag);
	if (rev && rev[0])
		d->rev = xstrdup(rev);
	if (branch && branch[0])
		d->branch = xstrdup(branch);
	if (subdir && subdir[0])
		d->subdir = xstrdup(subdir);
	return 0;
}

// Free all strings owned by a dependency spec and zero the struct.
static void
dep_free(DepSpec* d) {
	free(d->name);
	free(d->git);
	free(d->tag);
	free(d->rev);
	free(d->branch);
	free(d->subdir);
	memset(d, 0, sizeof(*d));
}

// Deep-copy a dependency spec for queueing without aliasing mutable strings.
static void
dep_dup(DepSpec* dst, DepSpec* src) {
	memset(dst, 0, sizeof(*dst));
	dst->name = xstrdup(src->name);
	dst->git = xstrdup(src->git);
	if (src->tag)
		dst->tag = xstrdup(src->tag);
	if (src->rev)
		dst->rev = xstrdup(src->rev);
	if (src->branch)
		dst->branch = xstrdup(src->branch);
	if (src->subdir)
		dst->subdir = xstrdup(src->subdir);
}

// Pin a dependency to a concrete commit: use rev as-is or resolve tag/branch via git.
static int
resolve_git_rev(DepSpec* d, char* revout, size_t nrev, VendorCtx* ctx) {
	char cmd[4096], got[128];

	if (d->rev) {
		snprintf(revout, nrev, "%s", d->rev);
		return 0;
	}
	if (d->tag) {
		snprintf(cmd, sizeof(cmd),
			 "git ls-remote --refs \"%s\" \"refs/tags/%s\" 2>%s",
			 d->git, d->tag, host_devnull());
		if (run_capture(got, sizeof(got), cmd)) {
			fprintf(stderr, "modc vendor: cannot resolve tag \"%s\" for %s\n",
				d->tag, d->git);
			return 1;
		}
		snprintf(revout, nrev, "%s", got);
		if (ctx->verbose)
			fprintf(stderr, "vendor: %s tag %s -> %s\n", d->name, d->tag, revout);
		return 0;
	}
	if (d->branch) {
		snprintf(cmd, sizeof(cmd),
			 "git ls-remote \"%s\" \"refs/heads/%s\" 2>%s",
			 d->git, d->branch, host_devnull());
		if (run_capture(got, sizeof(got), cmd)) {
			fprintf(stderr, "modc vendor: cannot resolve branch \"%s\" for %s\n",
				d->branch, d->git);
			return 1;
		}
		snprintf(revout, nrev, "%s", got);
		if (ctx->verbose)
			fprintf(stderr, "vendor: %s branch %s -> %s\n", d->name, d->branch, revout);
		return 0;
	}
	fprintf(stderr, "modc vendor: missing tag, rev, or branch for \"%s\"\n",
		d->name ? d->name : d->git);
	return 1;
}

// Compare two git remote URLs for exact equality.
static int
same_git_url(const char* a, const char* b) {
	return a && b && strcmp(a, b) == 0;
}

// Index of a locked package by import name, or -1 when not yet resolved.
static int
locked_find(LockedPkg* pkgs, int n, const char* name) {
	int i;

	for (i = 0; i < n; i++)
		if (strcmp(pkgs[i].name, name) == 0)
			return i;
	return -1;
}

// Record a resolved package in the lock set; rejects duplicate names with conflicting git/rev.
static int
locked_add(LockedPkg* pkgs, int* np, DepSpec* d, const char* rev) {
	LockedPkg* p;
	int i;

	i = locked_find(pkgs, *np, d->name);
	if (i >= 0) {
		p = &pkgs[i];
		if (!same_git_url(p->git, d->git)) {
			fprintf(stderr,
				"modc vendor: dependency conflict for \"%s\":\n"
				"  %s\n"
				"  %s\n",
				d->name, p->git, d->git);
			return 1;
		}
		if (strcmp(p->rev, rev) != 0) {
			fprintf(stderr,
				"modc vendor: version conflict for \"%s\" (%s vs %s)\n",
				d->name, p->rev, rev);
			return 1;
		}
		return 0;
	}
	if (*np >= MaxLocked)
		return 1;
	p = &pkgs[*np];
	p->name = xstrdup(d->name);
	p->git = xstrdup(d->git);
	p->rev = xstrdup(rev);
	p->subdir = d->subdir ? xstrdup(d->subdir) : xstrdup("");
	(*np)++;
	return 0;
}

// Free every locked-package entry in a resolved graph.
static void
locked_free_all(LockedPkg* pkgs, int n) {
	int i;

	for (i = 0; i < n; i++) {
		free(pkgs[i].name);
		free(pkgs[i].git);
		free(pkgs[i].rev);
		free(pkgs[i].subdir);
	}
}

// Collect all [prefix.NAME] sections into a DepSpec array (e.g. deps.foo → name "foo").
static int
parse_deps_ini(IniFile* ini, const char* prefix, DepSpec** out, int* out_len) {
	int i, n, plen, err;
	DepSpec* list;
	IniSect* s;
	const char* p;

	plen = (int)strlen(prefix);
	n = 0;
	for (i = 0; i < ini->nsect; i++) {
		if (strncmp(ini->sect[i].name, prefix, plen) == 0 && ini->sect[i].name[plen] == '.' && ini->sect[i].name[plen + 1])
			n++;
	}
	if (n == 0) {
		*out = NULL;
		*out_len = 0;
		return 0;
	}
	list = xmalloc(n * sizeof(DepSpec));
	n = 0;
	for (i = 0; i < ini->nsect; i++) {
		s = &ini->sect[i];
		if (strncmp(s->name, prefix, plen) != 0 || s->name[plen] != '.')
			continue;
		p = s->name + plen + 1;
		if (p[0] == 0)
			continue;
		memset(&list[n], 0, sizeof(list[n]));
		list[n].name = xstrdup(p);
		err = dep_from_sect(&list[n], s);
		if (err == 1) {
			fprintf(stderr, "modc vendor: [%s] requires git = URL\n", s->name);
			goto fail;
		}
		if (err == 2) {
			fprintf(stderr,
				"modc vendor: [%s] requires exactly one of tag, rev, branch\n",
				s->name);
			goto fail;
		}
		n++;
	}
	*out = list;
	*out_len = n;
	return 0;

fail:
	while (n > 0)
		dep_free(&list[--n]);
	free(list);
	return 1;
}

// Free an array of dependency specs allocated by parse_deps_ini.
static void
deps_free(DepSpec* deps, int n) {
	int i;

	for (i = 0; i < n; i++)
		dep_free(&deps[i]);
	free(deps);
}

// Clone a git repo into a fresh temp dir and check out the pinned revision.
static int
clone_to_temp(DepSpec* d, const char* rev, char* tmpdir, size_t ntmp, VendorCtx* ctx) {
	char cmd[8192];

	if (host_mkdtemp(tmpdir, ntmp, "modc-vendor") != 0) {
		fprintf(stderr, "modc vendor: cannot create temp dir: %s\n", strerror(errno));
		return 1;
	}
	snprintf(cmd, sizeof(cmd), "git clone --quiet \"%s\" \"%s\" 2>%s", d->git, tmpdir,
		 host_devnull());
	if (ctx->verbose)
		fprintf(stderr, "vendor: git clone %s\n", d->git);
	if (run_status(cmd)) {
		fprintf(stderr, "modc vendor: git clone failed for \"%s\"\n", d->git);
		return 1;
	}
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" checkout --quiet \"%s\" 2>%s", tmpdir, rev,
		 host_devnull());
	if (run_status(cmd)) {
		fprintf(stderr, "modc vendor: git checkout %s failed for \"%s\"\n", rev, d->git);
		return 1;
	}
	return 0;
}

// Load transitive [deps.*] from a vendored tree's modc.ini, if present.
static int
read_transitive(const char* srcdir, DepSpec** out, int* out_len) {
	char path[1024];
	IniFile ini;

	snprintf(path, sizeof(path), "%s/modc.ini", srcdir);
	if (host_access_read(path) != 0) {
		*out = NULL;
		*out_len = 0;
		return 0;
	}
	if (ini_load(&ini, path)) {
		fprintf(stderr, "modc vendor: cannot read %s\n", path);
		return 1;
	}
	if (parse_deps_ini(&ini, "deps", out, out_len)) {
		ini_free(&ini);
		return 1;
	}
	ini_free(&ini);
	return 0;
}

// Breadth-first resolve of the full dependency graph, deduping by import name.
static int
resolve_graph(VendorCtx* ctx, DepSpec* roots, int nroots, LockedPkg* out, int* out_len) {
	DepSpec queue[MaxQueue];
	int qhead, qtail, nq, i, ti;
	LockedPkg pkgs[MaxLocked];
	int npkgs;
	char rev[128], tmpdir[HOST_PATH_MAX];
	char srcdir[1024];
	DepSpec* trans;
	int ntrans;

	memset(pkgs, 0, sizeof(pkgs));
	npkgs = 0;
	nq = 0;
	for (i = 0; i < nroots; i++) {
		if (nq >= MaxQueue)
			return 1;
		dep_dup(&queue[nq], &roots[i]);
		nq++;
	}
	qhead = 0;
	qtail = nq;
	while (qhead < qtail) {
		DepSpec cur;

		cur = queue[qhead];
		qhead++;
		if (locked_find(pkgs, npkgs, cur.name) >= 0) {
			dep_free(&cur);
			continue;
		}
		if (resolve_git_rev(&cur, rev, sizeof(rev), ctx)) {
			dep_free(&cur);
			goto qfail;
		}
		if (locked_add(pkgs, &npkgs, &cur, rev)) {
			dep_free(&cur);
			goto qfail;
		}
		if (clone_to_temp(&cur, rev, tmpdir, sizeof(tmpdir), ctx)) {
			dep_free(&cur);
			goto qfail;
		}
		if (cur.subdir && cur.subdir[0])
			snprintf(srcdir, sizeof(srcdir), "%s/%s", tmpdir, cur.subdir);
		else
			snprintf(srcdir, sizeof(srcdir), "%s", tmpdir);
		if (!is_dir_path(srcdir)) {
			fprintf(stderr, "modc vendor: subdir \"%s\" not found in %s\n",
				cur.subdir ? cur.subdir : "", cur.git);
			rm_rf(tmpdir);
			dep_free(&cur);
			goto qfail;
		}
		if (read_transitive(srcdir, &trans, &ntrans)) {
			rm_rf(tmpdir);
			dep_free(&cur);
			goto qfail;
		}
		for (ti = 0; ti < ntrans; ti++) {
			if (qtail >= MaxQueue) {
				deps_free(trans, ntrans);
				rm_rf(tmpdir);
				dep_free(&cur);
				goto qfail;
			}
			dep_dup(&queue[qtail], &trans[ti]);
			qtail++;
		}
		deps_free(trans, ntrans);
		if (materialize_cloned(ctx, &pkgs[npkgs - 1], srcdir)) {
			rm_rf(tmpdir);
			dep_free(&cur);
			goto qfail;
		}
		rm_rf(tmpdir);
		dep_free(&cur);
	}
	for (i = qhead; i < qtail; i++)
		dep_free(&queue[i]);
	for (i = 0; i < npkgs; i++)
		out[i] = pkgs[i];
	*out_len = npkgs;
	return 0;

qfail:
	for (i = qhead; i < qtail; i++)
		dep_free(&queue[i]);
	return 1;
}

// Write modc.lock with pinned git URLs, revisions, and optional subdirs.
static int
write_lock(VendorCtx* ctx, LockedPkg* pkgs, int n) {
	char path[1024];
	FILE* f;
	int i;

	snprintf(path, sizeof(path), "%s/modc.lock", ctx->root);
	f = fopen(path, "wb");
	if (f == NULL) {
		fprintf(stderr, "modc vendor: cannot write %s: %s\n", path, strerror(errno));
		return 1;
	}
	fprintf(f, "# modc lockfile v1\n\n");
	for (i = 0; i < n; i++) {
		fprintf(f, "[pkg.%s]\n", pkgs[i].name);
		fprintf(f, "git = %s\n", pkgs[i].git);
		fprintf(f, "rev = %s\n", pkgs[i].rev);
		if (pkgs[i].subdir && pkgs[i].subdir[0])
			fprintf(f, "subdir = %s\n", pkgs[i].subdir);
		fprintf(f, "\n");
	}
	fclose(f);
	return 0;
}

// Read modc.lock into a LockedPkg array for check or materialize passes.
static int
load_lock(VendorCtx* ctx, LockedPkg* pkgs, int* out_len) {
	char path[1024];
	IniFile ini;
	int i, n;

	snprintf(path, sizeof(path), "%s/modc.lock", ctx->root);
	if (ini_load(&ini, path)) {
		fprintf(stderr, "modc vendor: cannot read %s\n", path);
		return 1;
	}
	n = 0;
	for (i = 0; i < ini.nsect; i++) {
		const char *git, *rev, *subdir;

		if (strncmp(ini.sect[i].name, "pkg.", 4) != 0)
			continue;
		if (n >= MaxLocked) {
			ini_free(&ini);
			return 1;
		}
		git = ini_get(&ini.sect[i], "git");
		rev = ini_get(&ini.sect[i], "rev");
		if (git == NULL || rev == NULL) {
			fprintf(stderr, "modc vendor: [%s] requires git and rev\n",
				ini.sect[i].name);
			ini_free(&ini);
			return 1;
		}
		pkgs[n].name = xstrdup(ini.sect[i].name + 4);
		pkgs[n].git = xstrdup(git);
		pkgs[n].rev = xstrdup(rev);
		subdir = ini_get(&ini.sect[i], "subdir");
		pkgs[n].subdir = xstrdup(subdir ? subdir : "");
		n++;
	}
	ini_free(&ini);
	*out_len = n;
	return 0;
}

// Recursively delete a path (temp clones and stale vendor trees).
static int
rm_rf(const char* path) {
	return host_rmtree(path);
}

// Copy a single file's contents from src to dst.
static int
copy_file(const char* src, const char* dst) {
	char* text;
	size_t n;
	FILE* f;

	text = read_file(src, &n);
	if (text == NULL)
		return 1;
	f = fopen(dst, "wb");
	if (f == NULL) {
		free(text);
		return 1;
	}
	fwrite(text, 1, n, f);
	fclose(f);
	free(text);
	return 0;
}

// Recursively copy a directory tree (or a lone file) into vendor/.
static int
copy_tree(const char* src, const char* dst) {
	HostDir* d;
	const char* name;
	char spath[1024], dpath[1024];

	if (host_is_file(src))
		return copy_file(src, dst);
	if (!host_is_dir(src))
		return 1;
	if (host_mkdir(dst) != 0)
		return 1;
	d = host_opendir(src);
	if (d == NULL)
		return 1;
	while ((name = host_readdir(d)) != NULL) {
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;
		snprintf(spath, sizeof(spath), "%s/%s", src, name);
		snprintf(dpath, sizeof(dpath), "%s/%s", dst, name);
		if (host_is_dir(spath)) {
			if (copy_tree(spath, dpath)) {
				host_closedir(d);
				return 1;
			}
		} else if (host_is_file(spath)) {
			if (copy_file(spath, dpath)) {
				host_closedir(d);
				return 1;
			}
		}
	}
	host_closedir(d);
	return 0;
}

// Record the pinned revision in vendor/NAME/.modc-vendor-rev for staleness checks.
static int
write_stamp(const char* pkgdir, const char* rev) {
	char path[1024];
	FILE* f;

	snprintf(path, sizeof(path), "%s/.modc-vendor-rev", pkgdir);
	f = fopen(path, "w");
	if (f == NULL)
		return 1;
	fprintf(f, "%s\n", rev);
	fclose(f);
	return 0;
}

// Read the revision stamp written at materialize time; fails when missing or empty.
static int
read_stamp(const char* pkgdir, char* rev, size_t nrev) {
	char path[1024];
	FILE* f;

	snprintf(path, sizeof(path), "%s/.modc-vendor-rev", pkgdir);
	f = fopen(path, "r");
	if (f == NULL)
		return 1;
	if (fgets(rev, (int)nrev, f) == NULL) {
		fclose(f);
		return 1;
	}
	fclose(f);
	rev[strcspn(rev, "\r\n")] = 0;
	return rev[0] == 0;
}

// Copy a cloned (sub)tree into vendor/NAME and stamp the pinned revision.
static int
materialize_cloned(VendorCtx* ctx, LockedPkg* p, const char* srcdir) {
	char dstdir[1024];

	snprintf(dstdir, sizeof(dstdir), "%s/%s", ctx->vendor, p->name);
	if (is_dir_path(dstdir))
		rm_rf(dstdir);
	if (copy_tree(srcdir, dstdir)) {
		fprintf(stderr, "modc vendor: cannot copy %s into vendor/%s\n",
			p->git, p->name);
		return 1;
	}
	if (write_stamp(dstdir, p->rev))
		return 1;
	if (ctx->verbose)
		fprintf(stderr, "vendor: materialized %s (%s)\n", p->name, p->rev);
	return 0;
}

// Verify vendor/ exists for every lock entry and matches the stamped revision.
static int
vendor_check(VendorCtx* ctx) {
	LockedPkg pkgs[MaxLocked];
	int i, n;
	char path[1024], rev[128];

	if (load_lock(ctx, pkgs, &n))
		return 1;
	for (i = 0; i < n; i++) {
		snprintf(path, sizeof(path), "%s/%s", ctx->vendor, pkgs[i].name);
		if (!is_dir_path(path)) {
			fprintf(stderr, "modc vendor --check: missing vendor/%s\n",
				pkgs[i].name);
			locked_free_all(pkgs, n);
			return 1;
		}
		if (read_stamp(path, rev, sizeof(rev)) || strcmp(rev, pkgs[i].rev) != 0) {
			fprintf(stderr,
				"modc vendor --check: vendor/%s is stale (run modc vendor)\n",
				pkgs[i].name);
			locked_free_all(pkgs, n);
			return 1;
		}
	}
	locked_free_all(pkgs, n);
	return 0;
}

// modc vendor entry: resolve modc.ini deps, write modc.lock, and populate vendor/.
int vendor_cmd(int argc, char** argv) {
	VendorCtx ctx;
	char inipath[1024], root[4096];
	IniFile ini;
	DepSpec* roots;
	int nroots, i, err;
	LockedPkg pkgs[MaxLocked];
	int npkgs;

	memset(&ctx, 0, sizeof(ctx));
	ctx.verbose = 0;
	ctx.check_only = 0;
	root[0] = 0;
	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			fprintf(stderr,
				"usage: modc vendor [options]\n"
				"\n"
				"Resolve modc.ini dependencies and populate vendor/.\n"
				"Writes modc.lock with pinned git revisions.\n"
				"\n"
				"Options:\n"
				"  -C DIR           project root (default: current directory)\n"
				"      --check      verify vendor/ matches modc.lock\n"
				"  -v, --verbose    print git operations\n"
				"  -h, --help       show help\n"
				"\n"
				"Requires git. Run from an app root with modc.ini.\n");
			return 0;
		}
		if (strcmp(argv[i], "-C") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "modc vendor: -C requires a directory\n");
				return 1;
			}
			if (host_abspath(argv[++i], root, sizeof(root)) != 0)
				return 1;
			continue;
		}
		if (strcmp(argv[i], "--check") == 0) {
			ctx.check_only = 1;
			continue;
		}
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			ctx.verbose = 1;
			continue;
		}
		fprintf(stderr, "modc vendor: unknown option %s\n", argv[i]);
		return 1;
	}
	if (root[0] == 0) {
		if (host_getcwd(root, sizeof(root)) != 0) {
			fprintf(stderr, "modc vendor: getcwd: %s\n", strerror(errno));
			return 1;
		}
	}
	snprintf(ctx.root, sizeof(ctx.root), "%s", root);
	snprintf(ctx.vendor, sizeof(ctx.vendor), "%s/vendor", ctx.root);
	if (ctx.check_only)
		return vendor_check(&ctx);
	snprintf(inipath, sizeof(inipath), "%s/modc.ini", ctx.root);
	if (ini_load(&ini, inipath)) {
		fprintf(stderr, "modc vendor: cannot read %s\n", inipath);
		return 1;
	}
	if (parse_deps_ini(&ini, "deps", &roots, &nroots)) {
		ini_free(&ini);
		return 1;
	}
	ini_free(&ini);
	if (nroots == 0) {
		fprintf(stderr, "modc vendor: no [deps.NAME] sections in modc.ini\n");
		deps_free(roots, nroots);
		return 1;
	}
	npkgs = 0;
	if (host_mkdir(ctx.vendor) != 0) {
		fprintf(stderr, "modc vendor: cannot create vendor/: %s\n", strerror(errno));
		deps_free(roots, nroots);
		return 1;
	}
	err = resolve_graph(&ctx, roots, nroots, pkgs, &npkgs);
	deps_free(roots, nroots);
	if (err) {
		locked_free_all(pkgs, npkgs);
		return 1;
	}
	if (write_lock(&ctx, pkgs, npkgs)) {
		locked_free_all(pkgs, npkgs);
		return 1;
	}
	locked_free_all(pkgs, npkgs);
	return 0;
}
