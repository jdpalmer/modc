// os — process environment, cwd, clock, exit, Cmd, Poll. Not POSIX and not Win32.
//
// os_env views the process environment until the next os_set_env of that key
// (on Win32, until the next os_env or os_set_env). Empty val unsets. NUL
// copies use a 4 KiB stack buffer. os_cwd fills dst and returns a logical
// slash-path view. Cmd spawns argv (no shell). Poll waits on File and Cmd.
// POSIX uses curated host/include stubs (unistd, time, …), not system headers.
import "path";
import "fs";
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { OsCap = 4096 };

enum {
	StdIn = 0,
	StdOut = 1,
	StdErr = 2
};

enum {
	IoInherit = 0,
	IoNull = 1
};

enum {
	PollIn = 1,
	PollOut = 2,
	PollHup = 4,
	PollErr = 8
};

struct Cmd {
	static void* native;
};

struct Poll {
	static void* native;
};

static char[..] os_empty() {
	return ranged((char*)0, 0);
}

static int os_zcopy(const char[..] s, char* buf, size_t cap) {
	size_t n = len(s);
	if (n + 1 > cap || (n > 0 && ptr(s) == NULL)) {
		return 0;
	}
	if (n > 0) {
		memcpy(buf, ptr(s), n);
	}
	buf[n] = 0;
	return 1;
}

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#endif

(bool, char[..]) os_env(const char[..] key) {
#ifdef _WIN32
	static char buf[OsCap] = { 0 };
	char zkey[OsCap] = { 0 };
	unsigned n = 0;
	if (!os_zcopy(key, zkey, OsCap)) {
		return (false, os_empty());
	}
	n = GetEnvironmentVariableA(zkey, buf, OsCap);
	if (n == 0) {
		/* 203 = ERROR_ENVVAR_NOT_FOUND. 0 with no error is an empty value. */
		/* ERROR_ENVVAR_NOT_FOUND or other error → missing. 0 = empty value. */
		if (GetLastError() != 0) {
			return (false, os_empty());
		}
		return (true, ranged(buf, 0));
	}
	if (n >= OsCap) {
		return (false, os_empty());
	}
	return (true, ranged(buf, n));
#else
	char zkey[OsCap] = { 0 };
	char* v = NULL;
	if (!os_zcopy(key, zkey, OsCap)) {
		return (false, os_empty());
	}
	v = getenv(zkey);
	if (v == NULL) {
		return (false, os_empty());
	}
	return (true, ranged(v, strlen(v)));
#endif
}

bool os_set_env(const char[..] key, const char[..] val) {
#ifdef _WIN32
	char zkey[OsCap] = { 0 };
	char zval[OsCap] = { 0 };
	int i = 0;
	if (!os_zcopy(key, zkey, OsCap) || zkey[0] == 0) {
		return false;
	}
	while (zkey[i] != 0) {
		if (zkey[i] == '=') {
			return false;
		}
		i++;
	}
	if (len(val) == 0) {
		return SetEnvironmentVariableA(zkey, NULL) != 0;
	}
	if (!os_zcopy(val, zval, OsCap)) {
		return false;
	}
	return SetEnvironmentVariableA(zkey, zval) != 0;
#else
	char zkey[OsCap] = { 0 };
	char zval[OsCap] = { 0 };
	if (!os_zcopy(key, zkey, OsCap)) {
		return false;
	}
	if (len(val) == 0) {
		return unsetenv(zkey) == 0;
	}
	if (!os_zcopy(val, zval, OsCap)) {
		return false;
	}
	return setenv(zkey, zval, 1) == 0;
#endif
}

(char[..], bool) os_cwd(char[..] dst) {
#ifdef _WIN32
	char host[OsCap] = { 0 };
	unsigned n = 0;
	if (cap(dst) == 0 || ptr(dst) == NULL) {
		return (ranged((char*)0, 0), false);
	}
	n = GetCurrentDirectoryA(OsCap, host);
	if (n == 0 || n >= OsCap) {
		if (cap(dst) > 0) {
			ptr(dst)[0] = 0;
		}
		return (ranged((char*)0, 0), false);
	}
	return path_from_sys(dst, ranged(host, n));
#else
	size_t n = 0;
	if (cap(dst) == 0 || ptr(dst) == NULL) {
		return (ranged((char*)0, 0), false);
	}
	if (getcwd(ptr(dst), cap(dst)) == NULL) {
		if (cap(dst) > 0) {
			ptr(dst)[0] = 0;
		}
		return (ranged((char*)0, 0), false);
	}
	n = strlen(ptr(dst));
	return (ranged(ptr(dst), n, cap(dst)), true);
#endif
}

bool os_chdir(const char[..] path) {
#ifdef _WIN32
	char host[OsCap] = { 0 };
	{
		auto (view, ok) = path_to_sys(ranged(host, 0, OsCap), path);
		if (!ok || len(view) >= OsCap) {
			return false;
		}
	}
	return SetCurrentDirectoryA(host) != 0;
#else
	char zbuf[OsCap] = { 0 };
	if (!os_zcopy(path, zbuf, OsCap)) {
		return false;
	}
	return chdir(zbuf) == 0;
#endif
}

void os_sleep_ms(int64_t ms) {
#ifdef _WIN32
	unsigned u = 0;
	if (ms > 0) {
		if (ms > 1000000000) {
			u = 1000000000;
		} else {
			u = (unsigned)ms;
		}
	}
	Sleep(u);
#else
	struct timespec ts = { 0 };
	if (ms < 0) {
		ms = 0;
	}
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000)* 1000000;
	nanosleep(&ts, 0);
#endif
}

int64_t os_mono_ns() {
#ifdef _WIN32
	static int64_t freq = 0;
	int64_t now = 0;
	int64_t sec = 0;
	int64_t rem = 0;
	if (freq == 0) {
		if (!QueryPerformanceFrequency(&freq) || freq <= 0) {
			freq = 0;
			return 0;
		}
	}
	if (!QueryPerformanceCounter(&now)) {
		return 0;
	}
	sec = now / freq;
	rem = now % freq;
	return sec * 1000000000 + (rem * 1000000000) / freq;
#else
	struct timespec ts = { 0 };
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return ts.tv_sec * 1000000000L + ts.tv_nsec;
#endif
}

void os_exit(int code) {
	exit(code);
}

(char[..], bool) os_look_path(char[..] dst, const char[..] name) {
	char host[OsPathMax] = { 0 };
	size_t n = 0;
	if (cap(dst) == 0 || ptr(dst) == NULL || len(name) == 0 || ptr(name) == NULL) {
		return (os_empty(), false);
	}
	{
		char zname[OsPathMax] = { 0 };
		if (!os_zcopy(name, zname, OsPathMax)) {
			return (os_empty(), false);
		}
		if (!ocmd_resolve_path(zname, host, OsPathMax)) {
			return (os_empty(), false);
		}
	}
	n = strlen(host);
	if (n >= cap(dst)) {
		return (os_empty(), false);
	}
	memcpy(ptr(dst), host, n);
	if (cap(dst) > n) {
		ptr(dst)[n] = 0;
	}
	return (ranged(ptr(dst), n, cap(dst)), true);
}

void (Cmd* c).init(const char[..] path, const char[..]* args, size_t nargs) {
	OsCmd* blob = NULL;
	size_t i = 0;
	if (c.native != NULL) {
		ocmd_free((OsCmd *)c.native);
		c.native = NULL;
	}
	if (len(path) == 0 || ptr(path) == NULL) {
		return;
	}
	if (nargs > 0 && args == NULL) {
		return;
	}
	blob = ocmd_new();
	if (blob == NULL) {
		return;
	}
	if (!ocmd_init(blob, ptr(path), len(path))) {
		ocmd_free(blob);
		return;
	}
	for (i = 0; i < nargs; i++) {
		const char[..] a = args[i];
		if (!ocmd_add_arg(blob, ptr(a), len(a))) {
			ocmd_free(blob);
			return;
		}
	}
	c.native = blob;
}

bool (Cmd* c).dir(const char[..] d) {
	OsCmd* blob = NULL;
	if (c.native == NULL) {
		return false;
	}
	blob = (OsCmd *)c.native;
	if (len(d) == 0) {
		return ocmd_set_dir(blob, NULL, 0) != 0;
	}
	return ocmd_set_dir(blob, ptr(d), len(d)) != 0;
}

bool (Cmd* c).env(const char[..] key, const char[..] val) {
	OsCmd* blob = NULL;
	if (c.native == NULL || len(key) == 0) {
		return false;
	}
	blob = (OsCmd *)c.native;
	return ocmd_set_env(blob, ptr(key), len(key), ptr(val), len(val)) != 0;
}

bool (Cmd* c).stdio(int which, int mode) {
	OsCmd* blob = NULL;
	if (c.native == NULL) {
		return false;
	}
	blob = (OsCmd *)c.native;
	return ocmd_stdio(blob, which, mode) != 0;
}

(File, bool) (Cmd* c).pipe(int which) {
	File f = { 0 };
	OsCmd* blob = NULL;
	if (c.native == NULL) {
		return (f, false);
	}
	blob = (OsCmd *)c.native;
#ifdef _WIN32
	{
		HANDLE parent = 0;
		if (!ocmd_pipe(blob, which, &parent)) {
			return (f, false);
		}
		{
			auto (pf, ok) = file_from_handle((void*)parent);
			if (!ok) {
				CloseHandle(parent);
				return (f, false);
			}
			return (pf, true);
		}
	}
#else
	{
		int parent = -1;
		if (!ocmd_pipe(blob, which, &parent)) {
			return (f, false);
		}
		{
			auto (pf, ok) = file_from_fd(parent);
			if (!ok) {
				close(parent);
				return (f, false);
			}
			return (pf, true);
		}
	}
#endif
}

bool (Cmd* c).start() {
	OsCmd* blob = NULL;
	if (c.native == NULL) {
		return false;
	}
	blob = (OsCmd *)c.native;
	return ocmd_start(blob) != 0;
}

(int, bool) (Cmd* c).wait() {
	OsCmd* blob = NULL;
	int code = 0;
	if (c.native == NULL) {
		return (0, false);
	}
	blob = (OsCmd *)c.native;
	if (!ocmd_wait(blob, &code)) {
		return (0, false);
	}
	return (code, true);
}

void (Cmd* c).close() {
	if (c.native == NULL) {
		return;
	}
	ocmd_close((OsCmd *)c.native);
	ocmd_free((OsCmd *)c.native);
	c.native = NULL;
}

void (Poll* p).init() {
	OsPoll* blob = NULL;
	if (p.native != NULL) {
		free(p.native);
		p.native = NULL;
	}
	blob = (OsPoll *)malloc(sizeof(OsPoll));
	if (blob == NULL) {
		return;
	}
	opoll_init(blob);
	p.native = blob;
}

bool (Poll* p).add_file(File* f, int events) {
	OsPoll* blob = NULL;
	if (p.native == NULL || f == NULL) {
		return false;
	}
#ifdef _WIN32
	if (file_handle(f) == NULL) {
		return false;
	}
#else
	if (file_fd(f) < 0) {
		return false;
	}
#endif
	blob = (OsPoll *)p.native;
	return opoll_add_file(blob, f, events) != 0;
}

bool (Poll* p).add_cmd(Cmd* c) {
	OsPoll* blob = NULL;
	if (p.native == NULL || c == NULL || c.native == NULL) {
		return false;
	}
	blob = (OsPoll *)p.native;
	return opoll_add_cmd(blob, c, c.native) != 0;
}

(int, bool) (Poll* p).wait(int64_t timeout_ms) {
	OsPoll* blob = NULL;
	int i = 0;
	int n = 0;
	int t = 0;
	if (p.native == NULL) {
		return (0, false);
	}
	blob = (OsPoll *)p.native;
	if (timeout_ms < 0) {
		t = -1;
	} else if (timeout_ms > 86400000) {
		t = 86400000;
	} else {
		t = (int)timeout_ms;
	}
#ifdef _WIN32
	{
		HANDLE hs[OsPollMax] = { 0 };
		for (i = 0; i < OsPollMax; i++) {
			hs[i] = 0;
		}
		for (i = 0; i < blob.n; i++) {
			if (!blob.slot[i].is_cmd && blob.slot[i].file != NULL) {
				hs[i] = (HANDLE)file_handle((File *)blob.slot[i].file);
			}
		}
		n = opoll_wait(blob, hs, t);
	}
#else
	{
		int fds[OsPollMax] = { 0 };
		for (i = 0; i < OsPollMax; i++) {
			fds[i] = -1;
		}
		for (i = 0; i < blob.n; i++) {
			if (!blob.slot[i].is_cmd && blob.slot[i].file != NULL) {
				fds[i] = file_fd((File *)blob.slot[i].file);
			}
		}
		n = opoll_wait(blob, fds, t);
	}
#endif
	if (n < 0) {
		return (0, false);
	}
	return (n, true);
}

int (Poll* p).ready_count() {
	OsPoll* blob = NULL;
	if (p.native == NULL) {
		return 0;
	}
	blob = (OsPoll *)p.native;
	return blob.nready;
}

(File *, int) (Poll* p).ready_file(int i) {
	OsPoll* blob = NULL;
	int si = 0;
	if (p.native == NULL || i < 0) {
		return ((File *)0, 0);
	}
	blob = (OsPoll *)p.native;
	if (i >= blob.nready) {
		return ((File *)0, 0);
	}
	si = blob.ready_idx[i];
	if (blob.slot[si].is_cmd) {
		return ((File *)0, 0);
	}
	return ((File *)blob.slot[si].file, blob.slot[si].revents);
}

Cmd* (Poll* p).ready_cmd(int i) {
	OsPoll* blob = NULL;
	int si = 0;
	if (p.native == NULL || i < 0) {
		return NULL;
	}
	blob = (OsPoll *)p.native;
	if (i >= blob.nready) {
		return NULL;
	}
	si = blob.ready_idx[i];
	if (!blob.slot[si].is_cmd) {
		return NULL;
	}
	return (Cmd *)blob.slot[si].cmd;
}

void (Poll* p).reset() {
	OsPoll* blob = NULL;
	if (p.native == NULL) {
		return;
	}
	blob = (OsPoll *)p.native;
	opoll_reset(blob);
}

void (Poll* p).close() {
	if (p.native == NULL) {
		return;
	}
	free(p.native);
	p.native = NULL;
}
