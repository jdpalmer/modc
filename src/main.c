/*
 * CLI entry: usage, Compiler/path setup, shared flag parsing.
 *
 * Compilation pipeline: [lex → pp → parse → type check → emit → QBE]
 * Build/link lives in cli_build.c; subcommands in cli_cmd.c.
 */
#include "cli.h"
#include <time.h>

// Print subcommand or top-level usage and exit.
void
usage(const char* sub) {
	if (sub && strcmp(sub, "check") == 0) {
		fprintf(stderr,
			"usage: modc check [options] file...\n"
			"\n"
			"Parse and type-check %%C sources (no code generation).\n"
			"\n"
			"Options:\n"
			"  -D, --define NAME[=VALUE]   preprocessor macro\n"
			"  -I, --include-dir DIR       include search path\n"
			"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
			"  -F DIR                      framework search path (macOS)\n"
			"      --no-system-includes    omit host system include paths\n"
			"  -h, --help                  show help\n"
			"  -v, --verbose               verbose messages\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "emit") == 0) {
		fprintf(stderr,
			"usage: modc emit [options] file.mc\n"
			"\n"
			"Emit QBE IL for one translation unit.\n"
			"\n"
			"Options:\n"
			"  -o, --output FILE           write QBE (default: stdout)\n"
			"  -D, --define NAME[=VALUE]\n"
			"  -I, --include-dir DIR\n"
			"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
			"  -F DIR                      framework search path\n"
			"      --no-system-includes\n"
			"  -h, --help\n"
			"  -v, --verbose\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "build") == 0) {
		fprintf(stderr,
			"usage: modc build [options] [file.mc|dir] [-o prog] [-- cc-args...]\n"
			"\n"
			"Compile and link: modc -> qbe -> system cc.\n"
			"With no path, builds the current directory (all .mc sources).\n"
			"A directory is compiled like a package (mod.mc first if present).\n"
			"\n"
			"Options:\n"
			"  -o, --output PROG           output executable (default: from path name)\n"
			"  -D, --define NAME[=VALUE]\n"
			"  -I, --include-dir DIR\n"
			"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
			"  -F DIR                      framework search path\n"
			"      --no-system-includes\n"
			"  -h, --help\n"
			"  -v, --verbose               print invoked commands\n"
			"\n"
			"Arguments after -- are passed to the linker (e.g. -lglfw, -framework OpenGL).\n"
			"Package c_libs / frameworks / c_sources (#pragma) are applied automatically.\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "run") == 0) {
		fprintf(stderr,
			"usage: modc run [options] [file.mc|dir] [-- program-args...]\n"
			"\n"
			"Build a temporary executable, run it, then delete it.\n"
			"With no path, runs the current directory (same root rules as build).\n"
			"\n"
			"Options:\n"
			"  -D, --define NAME[=VALUE]\n"
			"  -I, --include-dir DIR\n"
			"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
			"  -F DIR                      framework search path\n"
			"      --no-system-includes\n"
			"  -h, --help\n"
			"  -v, --verbose\n"
			"\n"
			"Arguments after -- are passed to the program.\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "test") == 0) {
		fprintf(stderr,
			"usage: modc test [options] [dir|file_test.mc] [-- program-args...]\n"
			"\n"
			"Discover and run project tests (*_test.mc).\n"
			"Each test file is built and run like 'modc run' (must define main).\n"
			"With no path, scans the current directory (non-recursive).\n"
			"\n"
			"Options:\n"
			"  -D, --define NAME[=VALUE]\n"
			"  -I, --include-dir DIR\n"
			"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
			"  -F DIR                      framework search path\n"
			"      --no-system-includes\n"
			"      --corpus                run 'make check' in the current directory\n"
			"  -h, --help\n"
			"  -v, --verbose\n"
			"\n"
			"Exit 0 if all tests pass (or none found). *_test.mc files are\n"
			"excluded from 'modc build dir' package discovery.\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "doc") == 0) {
		fprintf(stderr,
			"usage: modc doc [options] [target]\n"
			"\n"
			"Print package API docs (Go-style // or /* */ above decls).\n"
			"Target: omitted or dir/pkg lists exports; Name or pkg.Name looks up.\n"
			"\n"
			"Options:\n"
			"  -D, --define NAME[=VALUE]\n"
			"  -I, --include-dir DIR\n"
			"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
			"  -F DIR                      framework search path\n"
			"      --no-system-includes\n"
			"  -h, --help\n"
			"  -v, --verbose\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "format") == 0) {
		fprintf(stderr,
			"usage: modc format file.mc...\n"
			"\n"
			"Rewrite %%C sources in place (K&R token style; tabs; no config).\n"
			"Preprocessor lines are left mostly alone. Comments are preserved.\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "vendor") == 0) {
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
			"  -h, --help       show help\n");
		exit(sub ? 0 : 1);
	}
	if (sub && strcmp(sub, "clean") == 0) {
		fprintf(stderr,
			"usage: modc clean [options] [path]\n"
			"\n"
			"Remove the .modc-cache directory for a project.\n"
			"Path defaults to the current directory; same root rules as build\n"
			"(modc.ini when present, else the entry directory).\n"
			"\n"
			"Options:\n"
			"  -h, --help\n"
			"  -v, --verbose    print the cache path removed\n");
		exit(sub ? 0 : 1);
	}
	fprintf(stderr,
		"modc %s — the %%C compiler (modc -> qbe -> cc)\n"
		"\n"
		"Usage:\n"
		"  modc check [options] file...\n"
		"  modc emit  [options] file.mc [-o file.qbe]\n"
		"  modc build [options] [file.mc|dir] [-o prog] [-- cc-args...]\n"
		"  modc run   [options] [file.mc|dir] [-- program-args...]\n"
		"  modc test  [options] [dir|file_test.mc] [-- program-args...]\n"
		"  modc doc   [options] [target]\n"
		"  modc format file.mc...\n"
		"  modc vendor [options]\n"
		"  modc clean [options] [path]\n"
		"  modc help [command]\n"
		"\n"
		"Options (common):\n"
		"  -D, --define NAME[=VALUE]   define preprocessor macro\n"
		"  -I, --include-dir DIR       add include search path\n"
		"  -M DIR                      package search path (also MODC_PATH; before stdlib)\n"
		"  -F DIR                      framework search path\n"
		"      --no-system-includes    omit host system include paths\n"
		"  -h, --help                  show help\n"
		"  -V, --version               show version\n"
		"  -v, --verbose               print invoked commands\n"
		"\n"
		"Run 'modc help <command>' for details.\n",
		MODC_VERSION);
	exit(sub ? 0 : 1);
}

// Print compiler version and exit.
void
version(void) {
	printf("modc %s\n", MODC_VERSION);
	exit(0);
}

// True if s is a known top-level modc subcommand name.
static int
is_subcmd(const char* s) {
	return s && (!strcmp(s, "check") || !strcmp(s, "emit") || !strcmp(s, "build") || !strcmp(s, "run") || !strcmp(s, "test") || !strcmp(s, "doc") || !strcmp(s, "format") || !strcmp(s, "vendor") || !strcmp(s, "clean") || !strcmp(s, "help"));
}

// Append a -I path to driver options (takes ownership of path pointer).
static void
add_inc(CliOpts* o, char* path) {
	if (o->incpaths_len % 8 == 0)
		o->incpaths = xrealloc(o->incpaths, (o->incpaths_len + 8) * sizeof(char*));
	o->incpaths[o->incpaths_len++] = path;
}

// Append an input path to the driver file list.
void
add_file(CliOpts* o, char* path) {
	if (o->files_len % 8 == 0)
		o->files = xrealloc(o->files, (o->files_len + 8) * sizeof(char*));
	o->files[o->files_len++] = path;
}

// Append a post-- linker/program argument.
static void
add_link(CliOpts* o, char* arg) {
	if (o->linkargv_len % 8 == 0)
		o->linkargv = xrealloc(o->linkargv, (o->linkargv_len + 8) * sizeof(char*));
	o->linkargv[o->linkargv_len++] = arg;
}

// True if path is an existing directory.
static int
dir_exists(const char* path) {
	return host_is_dir(path);
}

// True if path is an existing regular file.
static int
file_exists_path(const char* path) {
	return host_is_file(path);
}

/* Absolute path of this binary into out; 0 on success. */
static int
exe_path(char* out, size_t n, const char* argv0) {
	return host_exe_path(out, n, argv0) == 0 ? 0 : 1;
}

/*
 * Installed tree: <prefix>/bin/modc → <prefix>/lib/modc/include.
 * Returns a heap path, or NULL if that tree is not present (in-tree build).
 */
static char*
discover_install_include(const char* argv0) {
	char exe[PATH_MAX], dir[PATH_MAX], cand[PATH_MAX], probe[PATH_MAX];
	char* slash;

	if (exe_path(exe, sizeof(exe), argv0))
		return NULL;
	snprintf(dir, sizeof(dir), "%s", exe);
	slash = (char*)host_path_last_sep(dir);
	if (slash == NULL)
		return NULL;
	*slash = 0;
	snprintf(cand, sizeof(cand), "%s/%s", dir, MODC_REL_INCLUDE);
	if (host_realpath(cand, probe, sizeof(probe)) == 0)
		snprintf(cand, sizeof(cand), "%s", probe);
	snprintf(probe, sizeof(probe), "%s/stddef.h", cand);
	if (!dir_exists(cand) || !file_exists_path(probe))
		return NULL;
	return xstrdup(cand);
}

/*
 * Installed tree: <prefix>/bin/modc → <prefix>/lib/modc/pkg.
 * Returns a heap path, or NULL if that tree is not present (in-tree build).
 */
static char*
discover_install_pkg(const char* argv0) {
	char exe[PATH_MAX], dir[PATH_MAX], cand[PATH_MAX], probe[PATH_MAX];
	char* slash;

	if (exe_path(exe, sizeof(exe), argv0))
		return NULL;
	snprintf(dir, sizeof(dir), "%s", exe);
	slash = (char*)host_path_last_sep(dir);
	if (slash == NULL)
		return NULL;
	*slash = 0;
	snprintf(cand, sizeof(cand), "%s/%s", dir, MODC_REL_PKG);
	if (host_realpath(cand, probe, sizeof(probe)) == 0)
		snprintf(cand, sizeof(cand), "%s", probe);
	snprintf(probe, sizeof(probe), "%s/str/mod.mc", cand);
	if (!dir_exists(cand) || !file_exists_path(probe))
		return NULL;
	return xstrdup(cand);
}

// Set Compiler.modc_include from env, install tree, or baked-in path.
static void
init_modc_include(Compiler* c, const char* argv0) {
	char* inst;

	c->modc_include = getenv("MODC_INCLUDE");
	if (c->modc_include && c->modc_include[0])
		return;
	inst = discover_install_include(argv0);
	if (inst) {
		c->modc_include = inst;
		return;
	}
#ifdef MODC_INCLUDE
	c->modc_include = MODC_INCLUDE;
#endif
}

// Stdlib package root: MODC_PKG, install-relative lib/modc/pkg, or in-tree bake.
static void
init_modc_pkg(Compiler* c, const char* argv0) {
	char* inst;

	c->modc_pkg = getenv("MODC_PKG");
	if (c->modc_pkg && c->modc_pkg[0])
		return;
	inst = discover_install_pkg(argv0);
	if (inst) {
		c->modc_pkg = inst;
		return;
	}
#ifdef MODC_PKG
	c->modc_pkg = MODC_PKG;
#endif
}

// Register a host system include directory if it exists.
static void
add_sysinc(Compiler* c, const char* path) {
	if (!dir_exists(path))
		return;
	if (c->sysincpaths_len % 8 == 0)
		c->sysincpaths = xrealloc(c->sysincpaths,
					  (c->sysincpaths_len + 8) * sizeof(char*));
	c->sysincpaths[c->sysincpaths_len++] = xstrdup(path);
}

// Split a PATH-like include list (:; on Win) into system includes.
static void
add_sysinc_from_env(Compiler* c, const char* list) {
	char buf[PATH_MAX], *p, *start;
	size_t n;

	if (list == NULL || list[0] == 0)
		return;
	start = xstrdup(list);
	for (p = start; *p;) {
		char* sep;
#ifdef _WIN32
		/* INCLUDE uses ';'; do not treat drive-letter ':' as a separator. */
		sep = strchr(p, ';');
#else
		sep = strchr(p, ':');
#endif
		if (sep)
			n = (size_t)(sep - p);
		else
			n = strlen(p);
		if (n >= sizeof(buf))
			n = sizeof(buf) - 1;
		memcpy(buf, p, n);
		buf[n] = 0;
		add_sysinc(c, buf);
		if (sep == NULL)
			break;
		p = sep + 1;
	}
	free(start);
}

// Add $root/usr/include and $root/usr/local/include when present.
static void
add_sysroot_includes(Compiler* c, const char* root) {
	char buf[PATH_MAX];

	if (root == NULL || root[0] == 0)
		return;
	snprintf(buf, sizeof(buf), "%s/usr/include", root);
	add_sysinc(c, buf);
	snprintf(buf, sizeof(buf), "%s/usr/local/include", root);
	add_sysinc(c, buf);
}

#ifdef __APPLE__
// Register a host -L library search directory (macOS).
static void
add_syslib(Compiler* c, const char* path) {
	if (path == NULL || path[0] == 0)
		return;
	if (c->syslibpaths_len % 8 == 0)
		c->syslibpaths = xrealloc(c->syslibpaths,
					  (c->syslibpaths_len + 8) * sizeof(char*));
	c->syslibpaths[c->syslibpaths_len++] = xstrdup(path);
}

/* Add $prefix/include and $prefix/lib when present. Returns 1 if either exists. */
static int
add_homebrew_prefix(Compiler* c, const char* prefix, int verbose) {
	char buf[PATH_MAX];
	int found;

	if (prefix == NULL || prefix[0] == 0)
		return 0;
	found = 0;
	snprintf(buf, sizeof(buf), "%s/include", prefix);
	if (dir_exists(buf)) {
		if (verbose)
			fprintf(stderr, "system include: %s\n", buf);
		add_sysinc(c, buf);
		found = 1;
	}
	snprintf(buf, sizeof(buf), "%s/lib", prefix);
	if (dir_exists(buf)) {
		if (verbose)
			fprintf(stderr, "library path: %s\n", buf);
		add_syslib(c, buf);
		found = 1;
	}
	return found;
}

// Probe HOMEBREW_PREFIX then common Homebrew prefixes for -I/-L.
static void
discover_homebrew_paths(Compiler* c, int verbose) {
	static const char* fallback[] = {
	    "/opt/homebrew", /* Apple Silicon */
	    "/usr/local",    /* Intel Homebrew / common prefix */
	    NULL};
	const char* env;
	int i;

	env = getenv("HOMEBREW_PREFIX");
	if (env && env[0] && add_homebrew_prefix(c, env, verbose))
		return;
	for (i = 0; fallback[i]; i++)
		add_homebrew_prefix(c, fallback[i], verbose);
}

// Add macOS SDK includes/frameworks via xcrun, then Homebrew.
static void
discover_macos_sysincludes(Compiler* c, int verbose) {
	FILE* fp;
	char sdk[PATH_MAX], buf[PATH_MAX];

	fp = popen("xcrun --show-sdk-path 2>/dev/null", "r");
	if (fp != NULL) {
		if (fgets(sdk, sizeof(sdk), fp) != NULL) {
			sdk[strcspn(sdk, "\r\n")] = 0;
			snprintf(buf, sizeof(buf), "%s/usr/include", sdk);
			if (verbose)
				fprintf(stderr, "system include: %s\n", buf);
			add_sysinc(c, buf);
			snprintf(buf, sizeof(buf), "%s/System/Library/Frameworks", sdk);
			if (dir_exists(buf)) {
				if (verbose)
					fprintf(stderr, "framework path: %s\n", buf);
				comp_add_framework_path(c, buf);
			}
		}
		pclose(fp);
	}
	discover_homebrew_paths(c, verbose);
}
#endif

#ifdef __linux__
// Add common Linux multiarch and /usr/include paths.
static void
discover_linux_sysincludes(Compiler* c, int verbose) {
	static const char* paths[] = {
	    "/usr/local/include",
	    "/usr/include/x86_64-linux-gnu",
	    "/usr/include/aarch64-linux-gnu",
	    "/usr/include/riscv64-linux-gnu",
	    "/usr/include",
	    NULL,
	};
	int i;

	for (i = 0; paths[i]; i++) {
		if (!dir_exists(paths[i]))
			continue;
		if (verbose)
			fprintf(stderr, "system include: %s\n", paths[i]);
		add_sysinc(c, paths[i]);
	}
}
#endif

#ifdef _WIN32
// Add Windows SDK Include/<ver>/{ucrt,shared,um,winrt} dirs.
static void
add_win_sdk_includes(Compiler* c, const char* sdk, const char* ver, int verbose) {
	char buf[PATH_MAX];
	static const char* parts[] = {"ucrt", "shared", "um", "winrt", NULL};
	int i;
	size_t nsdk, nver;

	if (sdk == NULL || sdk[0] == 0 || ver == NULL || ver[0] == 0)
		return;
	nsdk = strlen(sdk);
	nver = strlen(ver);
	while (nver > 0 && (ver[nver - 1] == '\\' || ver[nver - 1] == '/'))
		nver--;
	for (i = 0; parts[i]; i++) {
		if (snprintf(buf, sizeof(buf), "%.*sInclude\\%.*s\\%s", (int)nsdk, sdk,
			     (int)nver, ver, parts[i]) >= (int)sizeof(buf))
			continue;
		host_path_slashify(buf);
		if (!dir_exists(buf))
			continue;
		if (verbose)
			fprintf(stderr, "system include: %s\n", buf);
		add_sysinc(c, buf);
	}
}

// Fill system includes from INCLUDE or VS/SDK env vars.
static void
discover_windows_sysincludes(Compiler* c, int verbose) {
	const char *inc, *sdk, *ver, *vc;
	char buf[PATH_MAX];
	int before;

	inc = getenv("INCLUDE");
	if (inc && inc[0]) {
		before = c->sysincpaths_len;
		add_sysinc_from_env(c, inc);
		if (verbose) {
			int i;

			for (i = before; i < c->sysincpaths_len; i++)
				fprintf(stderr, "system include: %s\n", c->sysincpaths[i]);
		}
		return;
	}
	vc = getenv("VCToolsInstallDir");
	if (vc && vc[0]) {
		size_t n = strlen(vc);
		int need_sep = n > 0 && !host_path_is_sep((unsigned char)vc[n - 1]);

		if (snprintf(buf, sizeof(buf), "%s%sinclude", vc, need_sep ? "/" : "") <
		    (int)sizeof(buf)) {
			host_path_slashify(buf);
			if (dir_exists(buf)) {
				if (verbose)
					fprintf(stderr, "system include: %s\n", buf);
				add_sysinc(c, buf);
			}
		}
	}
	sdk = getenv("WindowsSdkDir");
	ver = getenv("WindowsSDKVersion");
	if (sdk && ver)
		add_win_sdk_includes(c, sdk, ver, verbose);
}
#endif

// Resolve host system includes (MODC_SYSINCLUDE / platform defaults).
static void
discover_sysincludes(Compiler* c, int verbose) {
	const char* env;

	env = getenv("MODC_SYSINCLUDE");
	if (env && env[0]) {
		add_sysinc_from_env(c, env);
		return;
	}
	if (c->no_system_includes)
		return;
	env = getenv("MODC_SYSROOT");
	if (env && env[0]) {
		add_sysroot_includes(c, env);
		return;
	}
#ifdef __APPLE__
	discover_macos_sysincludes(c, verbose);
#endif
#ifdef __linux__
	discover_linux_sysincludes(c, verbose);
#endif
#ifdef _WIN32
	discover_windows_sysincludes(c, verbose);
#endif
}

// Copy driver opts into Compiler and discover system includes.
void
apply_cli(Compiler* c, CliOpts* o) {
	int i;

	c->no_system_includes = o->no_system_includes;
	for (i = 0; i < o->incpaths_len; i++) {
		if (c->incpaths_len % 8 == 0)
			c->incpaths = xrealloc(c->incpaths, (c->incpaths_len + 8) * sizeof(char*));
		c->incpaths[c->incpaths_len++] = o->incpaths[i];
	}
	c->check_only = o->check_only;
	discover_sysincludes(c, o->verbose);
	if (o->verbose && c->modc_include && c->modc_include[0])
		fprintf(stderr, "modc include: %s\n", c->modc_include);
	if (o->verbose && c->modc_pkg && c->modc_pkg[0])
		fprintf(stderr, "modc pkg: %s\n", c->modc_pkg);
}

// Parse shared CLI flags into CliOpts/Compiler; -1 help, 1 error, 0 ok.
int
parse_common(Compiler* c, CliOpts* o, int* i, int argc, char** argv, int need_out) {
	for (; *i < argc; (*i)++) {
		char* a = argv[*i];

		if (strcmp(a, "--") == 0) {
			(*i)++;
			break;
		}
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
			return -1;
		if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0)
			version();
		if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
			o->verbose = 1;
			continue;
		}
		if (strcmp(a, "-D") == 0 || strcmp(a, "--define") == 0) {
			if (*i + 1 >= argc) {
				fprintf(stderr, "modc: %s requires an argument\n", a);
				return 1;
			}
			pp_define_cli(c, argv[++(*i)]);
			continue;
		}
		if (strncmp(a, "-D", 2) == 0 && a[2]) {
			pp_define_cli(c, a + 2);
			continue;
		}
		if (strcmp(a, "-I") == 0 || strcmp(a, "--include-dir") == 0) {
			if (*i + 1 >= argc) {
				fprintf(stderr, "modc: %s requires an argument\n", a);
				return 1;
			}
			add_inc(o, argv[++(*i)]);
			continue;
		}
		if (strncmp(a, "-I", 2) == 0 && a[2]) {
			add_inc(o, a + 2);
			continue;
		}
		if (strcmp(a, "-M") == 0) {
			if (*i + 1 >= argc) {
				fprintf(stderr, "modc: %s requires an argument\n", a);
				return 1;
			}
			pkg_add_search_path(c, argv[++(*i)]);
			continue;
		}
		if (strncmp(a, "-M", 2) == 0 && a[2]) {
			pkg_add_search_path(c, a + 2);
			continue;
		}
		if (strcmp(a, "-F") == 0) {
			if (*i + 1 >= argc) {
				fprintf(stderr, "modc: %s requires an argument\n", a);
				return 1;
			}
			comp_add_framework_path(c, argv[++(*i)]);
			continue;
		}
		if (strncmp(a, "-F", 2) == 0 && a[2]) {
			comp_add_framework_path(c, a + 2);
			continue;
		}
		if (strcmp(a, "--no-system-includes") == 0) {
			o->no_system_includes = 1;
			continue;
		}
		if (strcmp(a, "--corpus") == 0) {
			o->corpus = 1;
			continue;
		}
		if ((strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) && need_out) {
			if (*i + 1 >= argc) {
				fprintf(stderr, "modc: %s requires an argument\n", a);
				return 1;
			}
			o->output = argv[++(*i)];
			continue;
		}
		if (a[0] == '-')
			fprintf(stderr, "modc: unknown option '%s'\n", a);
		else {
			add_file(o, a);
			continue;
		}
		return 1;
	}
	for (; *i < argc; (*i)++)
		add_link(o, argv[*i]);
	return 0;
}

// CLI entry: dispatch subcommands after Compiler/path setup.
int main(int argc, char** argv) {
	Compiler c;
	CliOpts o;
	const char* sub;

	memset(&c, 0, sizeof(c));
	memset(&o, 0, sizeof(o));
	if (getenv("MODC_NO_SYSTEM_INCLUDES"))
		o.no_system_includes = 1;
	init_modc_include(&c, argv[0]);
	init_modc_pkg(&c, argv[0]);
	if (argc < 2)
		usage(NULL);
	if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0)
		version();
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
		usage(NULL);
	if (!is_subcmd(argv[1])) {
		fprintf(stderr, "modc: unknown command '%s'\n", argv[1]);
		usage(NULL);
	}
	sub = argv[1];
	if (strcmp(sub, "help") == 0) {
		if (argc >= 3)
			usage(argv[2]);
		usage(NULL);
	}
	if (strcmp(sub, "check") == 0)
		return cmd_check(&c, &o, argc, argv);
	if (strcmp(sub, "emit") == 0)
		return cmd_emit(&c, &o, argc, argv);
	if (strcmp(sub, "build") == 0)
		return cmd_build(&c, &o, argc, argv);
	if (strcmp(sub, "run") == 0)
		return cmd_run(&c, &o, argc, argv);
	if (strcmp(sub, "test") == 0)
		return cmd_test(&c, &o, argc, argv);
	if (strcmp(sub, "doc") == 0)
		return cmd_doc(&c, &o, argc, argv);
	if (strcmp(sub, "format") == 0)
		return cmd_format(&c, &o, argc, argv);
	if (strcmp(sub, "vendor") == 0)
		return vendor_cmd(argc, argv);
	if (strcmp(sub, "clean") == 0)
		return cmd_clean(&o, argc, argv);
	usage(NULL);
	return 1;
}

