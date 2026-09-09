/*
 * Host OS helpers for the modc driver (paths, dirs, processes).
 */
#include "host_os.h"
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

struct HostDir {
#ifdef _WIN32
	HANDLE h;
	WIN32_FIND_DATAA fd;
	int first;
	char name[MAX_PATH];
#else
	DIR* d;
#endif
};

// True if c is a path separator (/ or \).
int
host_path_is_sep(int c) {
	return c == '/' || c == '\\';
}

// True if path is absolute on this host (drive letter or leading /).
int
host_path_is_abs(const char* path) {
	if (path == NULL || path[0] == 0)
		return 0;
#ifdef _WIN32
	if (host_path_is_sep((unsigned char)path[0]))
		return 1;
	if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
	    path[1] == ':')
		return 1;
	return 0;
#else
	return path[0] == '/';
#endif
}

// Pointer to the last path separator in path, or NULL.
const char*
host_path_last_sep(const char* path) {
	const char *p, *last;

	if (path == NULL)
		return NULL;
	last = NULL;
	for (p = path; *p; p++)
		if (host_path_is_sep((unsigned char)*p))
			last = p;
	return last;
}

// Normalize backslashes to / in place for portable cache keys.
void
host_path_slashify(char* path) {
	char* p;

	if (path == NULL)
		return;
	for (p = path; *p; p++)
		if (*p == '\\')
			*p = '/';
}

// Write the parent directory of path into out ("." if none).
void
host_dirname(const char* path, char* out, size_t n) {
	const char* slash;
	size_t len;

	if (path == NULL || path[0] == 0) {
		snprintf(out, n, ".");
		return;
	}
	slash = host_path_last_sep(path);
	if (slash == NULL) {
		snprintf(out, n, ".");
		return;
	}
	if (slash == path) {
		snprintf(out, n, "%c", *slash == '\\' ? '\\' : '/');
		return;
	}
#ifdef _WIN32
	/* "C:/" → keep drive root */
	if (slash == path + 2 && path[1] == ':') {
		snprintf(out, n, "%c:/", path[0]);
		return;
	}
#endif
	len = (size_t)(slash - path);
	if (len == 0)
		len = 1;
	if (len >= n)
		len = n - 1;
	memcpy(out, path, len);
	out[len] = 0;
}

// Heap-join a/b; absolute b replaces a.
char*
host_join_path(const char* a, const char* b) {
	size_t na, nb;
	char* p;

	if (b && host_path_is_abs(b))
		return xstrdup(b);
	na = a ? strlen(a) : 0;
	nb = b ? strlen(b) : 0;
	p = xmalloc(na + nb + 2);
	if (na == 0 || (na == 1 && a[0] == '.')) {
		memcpy(p, b ? b : "", nb + 1);
		return p;
	}
	memcpy(p, a, na);
	if (!host_path_is_sep((unsigned char)a[na - 1])) {
		p[na] = '/';
		memcpy(p + na + 1, b ? b : "", nb + 1);
	} else
		memcpy(p + na, b ? b : "", nb + 1);
	return p;
}

// Resolve path to an absolute string in out; 0 on success.
int
host_abspath(const char* path, char* out, size_t n) {
	char cwd[HOST_PATH_MAX], resolved[HOST_PATH_MAX];

	if (path == NULL || out == NULL || n == 0)
		return -1;
	if (host_path_is_abs(path)) {
		if (host_realpath(path, resolved, sizeof(resolved)) == 0)
			return snprintf(out, n, "%s", resolved) >= (int)n ? -1 : 0;
		return snprintf(out, n, "%s", path) >= (int)n ? -1 : 0;
	}
	if (host_getcwd(cwd, sizeof(cwd)) != 0)
		return -1;
	if (snprintf(resolved, sizeof(resolved), "%s/%s", cwd, path) >= (int)sizeof(resolved))
		return -1;
	if (host_realpath(resolved, out, n) == 0)
		return 0;
	return snprintf(out, n, "%s", resolved) >= (int)n ? -1 : 0;
}

#ifdef _WIN32

// Canonicalize existing path (realpath / GetFullPathName); 0 on success.
int
host_realpath(const char* path, char* out, size_t n) {
	char buf[HOST_PATH_MAX];
	DWORD r;

	if (path == NULL || out == NULL || n == 0)
		return -1;
	r = GetFullPathNameA(path, (DWORD)sizeof(buf), buf, NULL);
	if (r == 0 || r >= sizeof(buf))
		return -1;
	host_path_slashify(buf);
	if (strlen(buf) >= n)
		return -1;
	memcpy(out, buf, strlen(buf) + 1);
	return 0;
}

// Absolute path of the running modc binary into out.
int
host_exe_path(char* out, size_t n, const char* argv0) {
	char buf[HOST_PATH_MAX];
	DWORD r;

	(void)argv0;
	r = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
	if (r == 0 || r >= sizeof(buf))
		return -1;
	host_path_slashify(buf);
	if (strlen(buf) >= n)
		return -1;
	memcpy(out, buf, strlen(buf) + 1);
	return 0;
}

// Current working directory into out; 0 on success.
int
host_getcwd(char* out, size_t n) {
	if (_getcwd(out, (int)n) == NULL)
		return -1;
	host_path_slashify(out);
	return 0;
}

// True if path names an existing directory.
int
host_is_dir(const char* path) {
	DWORD a;

	if (path == NULL || path[0] == 0)
		return 0;
	a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// True if path names an existing regular file.
int
host_is_file(const char* path) {
	DWORD a;

	if (path == NULL || path[0] == 0)
		return 0;
	a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// True if path exists (file or directory).
int
host_exists(const char* path) {
	DWORD a;

	if (path == NULL || path[0] == 0)
		return 0;
	a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES;
}

// 0 if path is readable; -1 otherwise.
int
host_access_read(const char* path) {
	return _access(path, 4) == 0 ? 0 : -1;
}

// Create one directory (not parents); 0 if created or already exists.
int
host_mkdir(const char* path) {
	if (_mkdir(path) == 0)
		return 0;
	if (errno == EEXIST)
		return 0;
	return -1;
}

// Remove an empty directory; 0 on success.
int
host_rmdir(const char* path) {
	return _rmdir(path) == 0 ? 0 : -1;
}

// Delete a file; 0 on success.
int
host_unlink(const char* path) {
	return _unlink(path) == 0 ? 0 : -1;
}

// Create a unique temp directory named with prefix; path in out.
int
host_mkdtemp(char* out, size_t n, const char* prefix) {
	char tmp[MAX_PATH], path[MAX_PATH];
	unsigned i;
	DWORD tick;

	if (GetTempPathA((DWORD)sizeof(tmp), tmp) == 0)
		return -1;
	tick = GetTickCount();
	for (i = 0; i < 10000; i++) {
		snprintf(path, sizeof(path), "%s%s-%u", tmp, prefix ? prefix : "modc",
			 (unsigned)(tick + i));
		if (_mkdir(path) == 0) {
			host_path_slashify(path);
			if (strlen(path) >= n)
				return -1;
			memcpy(out, path, strlen(path) + 1);
			return 0;
		}
	}
	return -1;
}

// Open a directory for iteration, or NULL on failure.
HostDir*
host_opendir(const char* path) {
	HostDir* d;
	char pat[HOST_PATH_MAX];
	size_t n;

	if (path == NULL || path[0] == 0)
		path = ".";
	n = strlen(path);
	if (n + 3 >= sizeof(pat))
		return NULL;
	memcpy(pat, path, n + 1);
	if (n > 0 && !host_path_is_sep((unsigned char)pat[n - 1])) {
		pat[n] = '\\';
		pat[n + 1] = 0;
		n++;
	}
	memcpy(pat + n, "*", 2);
	d = xmalloc(sizeof(*d));
	d->h = FindFirstFileA(pat, &d->fd);
	if (d->h == INVALID_HANDLE_VALUE) {
		free(d);
		return NULL;
	}
	d->first = 1;
	return d;
}

// Next entry name in d, or NULL when exhausted.
const char*
host_readdir(HostDir* d) {
	if (d == NULL)
		return NULL;
	for (;;) {
		if (!d->first) {
			if (!FindNextFileA(d->h, &d->fd))
				return NULL;
		}
		d->first = 0;
		snprintf(d->name, sizeof(d->name), "%s", d->fd.cFileName);
		return d->name;
	}
}

// Close and free a HostDir from host_opendir.
void
host_closedir(HostDir* d) {
	if (d == NULL)
		return;
	if (d->h != INVALID_HANDLE_VALUE)
		FindClose(d->h);
	free(d);
}

// Recursively delete a file or directory tree; 0 on success.
static int
rmtree_one(const char* path) {
	HostDir* d;
	const char* name;
	char child[HOST_PATH_MAX];

	if (host_is_file(path))
		return host_unlink(path);
	if (!host_is_dir(path))
		return -1;
	d = host_opendir(path);
	if (d == NULL)
		return -1;
	while ((name = host_readdir(d)) != NULL) {
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, name) >= (int)sizeof(child)) {
			host_closedir(d);
			return -1;
		}
		if (rmtree_one(child) != 0) {
			host_closedir(d);
			return -1;
		}
	}
	host_closedir(d);
	return host_rmdir(path);
}

// Remove path recursively (file or directory tree).
int
host_rmtree(const char* path) {
	return rmtree_one(path);
}

// Run a shell command via system(); returns exit status or -1.
int
host_run(const char* cmd) {
	int st;

	st = system(cmd);
	if (st == -1)
		return -1;
	return st;
}

// Run cmd and capture the first whitespace-delimited token into out.
int
host_run_capture(char* out, size_t out_len, const char* cmd) {
	FILE* f;
	char line[512];
	size_t n;

	out[0] = 0;
	f = _popen(cmd, "r");
	if (f == NULL)
		return 1;
	if (fgets(line, sizeof(line), f) == NULL) {
		_pclose(f);
		return 1;
	}
	_pclose(f);
	n = strcspn(line, " \t\r\n");
	if (n >= out_len)
		n = out_len - 1;
	memcpy(out, line, n);
	out[n] = 0;
	return out[0] == 0;
}

// Spawn argv[0] with argv and wait; returns child exit status.
int
host_spawn_wait(char* const argv[]) {
	intptr_t r;

	if (argv == NULL || argv[0] == NULL)
		return -1;
	r = _spawnv(_P_WAIT, argv[0], (const char* const*)argv);
	if (r < 0)
		return -1;
	return (int)r;
}

// Host null device path ("nul" or "/dev/null").
const char*
host_devnull(void) {
	return "nul";
}

#else /* POSIX */

int
host_realpath(const char* path, char* out, size_t n) {
	char buf[HOST_PATH_MAX];

	if (path == NULL || out == NULL || n == 0)
		return -1;
	if (realpath(path, buf) == NULL)
		return -1;
	if (strlen(buf) >= n)
		return -1;
	memcpy(out, buf, strlen(buf) + 1);
	return 0;
}

// Absolute path of the running modc binary into out.
int
host_exe_path(char* out, size_t n, const char* argv0) {
	char buf[HOST_PATH_MAX], resolved[HOST_PATH_MAX];
#ifdef __APPLE__
	uint32_t sz;
#endif
	ssize_t r;

#ifdef __linux__
	r = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (r > 0) {
		buf[r] = 0;
		if (realpath(buf, resolved) != NULL) {
			snprintf(out, n, "%s", resolved);
			return 0;
		}
		snprintf(out, n, "%s", buf);
		return 0;
	}
#endif
#ifdef __APPLE__
	sz = (uint32_t)sizeof(buf);
	if (_NSGetExecutablePath(buf, &sz) == 0) {
		if (realpath(buf, resolved) != NULL) {
			snprintf(out, n, "%s", resolved);
			return 0;
		}
		snprintf(out, n, "%s", buf);
		return 0;
	}
#endif
	(void)r;
	if (argv0 == NULL || argv0[0] == 0)
		return -1;
	if (strchr(argv0, '/') != NULL) {
		if (realpath(argv0, resolved) != NULL) {
			snprintf(out, n, "%s", resolved);
			return 0;
		}
		snprintf(out, n, "%s", argv0);
		return 0;
	}
	return -1;
}

// Current working directory into out; 0 on success.
int
host_getcwd(char* out, size_t n) {
	return getcwd(out, n) == NULL ? -1 : 0;
}

// True if path names an existing directory.
int
host_is_dir(const char* path) {
	struct stat st;

	if (path == NULL || path[0] == 0)
		return 0;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// True if path names an existing regular file.
int
host_is_file(const char* path) {
	struct stat st;

	if (path == NULL || path[0] == 0)
		return 0;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// True if path exists (file or directory).
int
host_exists(const char* path) {
	struct stat st;

	return path && path[0] && stat(path, &st) == 0;
}

// 0 if path is readable; -1 otherwise.
int
host_access_read(const char* path) {
	return access(path, R_OK) == 0 ? 0 : -1;
}

// Create one directory (not parents); 0 if created or already exists.
int
host_mkdir(const char* path) {
	if (mkdir(path, 0755) == 0)
		return 0;
	if (errno == EEXIST)
		return 0;
	return -1;
}

// Remove an empty directory; 0 on success.
int
host_rmdir(const char* path) {
	return rmdir(path) == 0 ? 0 : -1;
}

// Delete a file; 0 on success.
int
host_unlink(const char* path) {
	return unlink(path) == 0 ? 0 : -1;
}

// Create a unique temp directory named with prefix; path in out.
int
host_mkdtemp(char* out, size_t n, const char* prefix) {
	char tmpl[256];

	snprintf(tmpl, sizeof(tmpl), "/tmp/%s-XXXXXX", prefix ? prefix : "modc");
	if (mkdtemp(tmpl) == NULL)
		return -1;
	if (strlen(tmpl) >= n)
		return -1;
	memcpy(out, tmpl, strlen(tmpl) + 1);
	return 0;
}

// Open a directory for iteration, or NULL on failure.
HostDir*
host_opendir(const char* path) {
	HostDir* d;
	DIR* dir;

	dir = opendir(path ? path : ".");
	if (dir == NULL)
		return NULL;
	d = xmalloc(sizeof(*d));
	d->d = dir;
	return d;
}

// Next entry name in d, or NULL when exhausted.
const char*
host_readdir(HostDir* d) {
	struct dirent* e;

	if (d == NULL || d->d == NULL)
		return NULL;
	e = readdir(d->d);
	return e ? e->d_name : NULL;
}

// Close and free a HostDir from host_opendir.
void
host_closedir(HostDir* d) {
	if (d == NULL)
		return;
	if (d->d)
		closedir(d->d);
	free(d);
}

// Recursively delete a file or directory tree; 0 on success.
static int
rmtree_one(const char* path) {
	HostDir* d;
	const char* name;
	char child[HOST_PATH_MAX];

	if (host_is_file(path))
		return host_unlink(path);
	if (!host_is_dir(path))
		return -1;
	d = host_opendir(path);
	if (d == NULL)
		return -1;
	while ((name = host_readdir(d)) != NULL) {
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, name) >= (int)sizeof(child)) {
			host_closedir(d);
			return -1;
		}
		if (rmtree_one(child) != 0) {
			host_closedir(d);
			return -1;
		}
	}
	host_closedir(d);
	return host_rmdir(path);
}

// Remove path recursively (file or directory tree).
int
host_rmtree(const char* path) {
	return rmtree_one(path);
}

// Run a shell command via system(); returns exit status or -1.
int
host_run(const char* cmd) {
	int st;

	st = system(cmd);
	if (st == -1)
		return -1;
	if (WIFEXITED(st))
		return WEXITSTATUS(st);
	return -1;
}

// Run cmd and capture the first whitespace-delimited token into out.
int
host_run_capture(char* out, size_t out_len, const char* cmd) {
	FILE* f;
	char line[512];
	size_t n;

	out[0] = 0;
	f = popen(cmd, "r");
	if (f == NULL)
		return 1;
	if (fgets(line, sizeof(line), f) == NULL) {
		pclose(f);
		return 1;
	}
	pclose(f);
	n = strcspn(line, " \t\r\n");
	if (n >= out_len)
		n = out_len - 1;
	memcpy(out, line, n);
	out[n] = 0;
	return out[0] == 0;
}

// Spawn argv[0] with argv and wait; returns child exit status.
int
host_spawn_wait(char* const argv[]) {
	pid_t pid;
	int st;

	if (argv == NULL || argv[0] == NULL)
		return -1;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &st, 0) < 0)
		return -1;
	if (WIFEXITED(st))
		return WEXITSTATUS(st);
	return -1;
}

// Host null device path ("nul" or "/dev/null").
const char*
host_devnull(void) {
	return "/dev/null";
}

#endif
