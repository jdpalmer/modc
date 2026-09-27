// Win32 helpers for os (package-internal).
#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

enum {
	OsIoInherit = 0,
	OsIoNull = 1,
	OsIoPipe = 2,
	OsStdIn = 0,
	OsStdOut = 1,
	OsStdErr = 2,
	OsPollMax = 16,
	OsArgMax = 256,
	OsEnvMax = 64,
	OsPathMax = 4096
};

struct OsCmd {
	char* file;
	char* argv[OsArgMax];
	int nargs;
	char* dir;
	char* ekey[OsEnvMax];
	char* eval[OsEnvMax];
	int nenv;
	int mode[3];
	HANDLE child_h[3];
	HANDLE proc;
	HANDLE thread;
	int started;
	int reaped;
	int status;
};

struct OsPollSlot {
	int is_cmd;
	void* file;
	void* cmd;
	void* ocmd;
	int events;
	int revents;
	int ready;
};

struct OsPoll {
	struct OsPollSlot slot[OsPollMax];
	int n;
	int nready;
	int ready_idx[OsPollMax];
};

void ocmd_clear_stdio(OsCmd* c) {
	int i = 0;
	for (i = 0; i < 3; i++) {
		c.mode[i] = OsIoInherit;
		if (c.child_h[i] != 0 && c.child_h[i] != INVALID_HANDLE_VALUE) {
			CloseHandle(c.child_h[i]);
			c.child_h[i] = 0;
		}
	}
}

OsCmd* ocmd_new() {
	OsCmd* c = 0;
	int i = 0;
	c = (OsCmd *)malloc(sizeof(*c));
	if (c == 0) {
		return 0;
	}
	memset(c, 0, sizeof(*c));
	for (i = 0; i < 3; i++) {
		c.mode[i] = OsIoInherit;
		c.child_h[i] = 0;
	}
	return c;
}

void ocmd_free_args(OsCmd* c) {
	int i = 0;
	for (i = 0; i < c.nargs; i++) {
		free(c.argv[i]);
		c.argv[i] = 0;
	}
	c.nargs = 0;
	free(c.file);
	c.file = 0;
}

void ocmd_free_env(OsCmd* c) {
	int i = 0;
	for (i = 0; i < c.nenv; i++) {
		free(c.ekey[i]);
		free(c.eval[i]);
		c.ekey[i] = 0;
		c.eval[i] = 0;
	}
	c.nenv = 0;
}

void ocmd_free(OsCmd* c) {
	if (c == 0) {
		return;
	}
	ocmd_clear_stdio(c);
	ocmd_free_args(c);
	ocmd_free_env(c);
	free(c.dir);
	if (c.proc) {
		CloseHandle(c.proc);
	}
	if (c.thread) {
		CloseHandle(c.thread);
	}
	free(c);
}

char* ocmd_strdup(const char *s, size_t n) {
	char* p = 0;
	p = (char*)malloc(n + 1);
	if (p == 0) {
		return 0;
	}
	if (n) {
		memcpy(p, s, n);
	}
	p[n] = 0;
	return p;
}

int ocmd_init(OsCmd* c, const char *path, size_t pathn) {
	if (c == 0 || path == 0 || pathn == 0) {
		return 0;
	}
	ocmd_free_args(c);
	c.file = ocmd_strdup(path, pathn);
	if (c.file == 0) {
		return 0;
	}
	c.argv[0] = ocmd_strdup(path, pathn);
	if (c.argv[0] == 0) {
		ocmd_free_args(c);
		return 0;
	}
	c.nargs = 1;
	c.argv[1] = 0;
	return 1;
}

int ocmd_add_arg(OsCmd* c, const char *s, size_t n) {
	if (c == 0 || c.nargs + 1 >= OsArgMax) {
		return 0;
	}
	c.argv[c.nargs] = ocmd_strdup(s ? s: "", n);
	if (c.argv[c.nargs] == 0) {
		return 0;
	}
	c.nargs++;
	c.argv[c.nargs] = 0;
	return 1;
}

int ocmd_set_dir(OsCmd* c, const char *d, size_t n) {
	free(c.dir);
	c.dir = 0;
	if (n == 0) {
		return 1;
	}
	c.dir = ocmd_strdup(d, n);
	return c.dir != 0;
}

int ocmd_set_env(OsCmd* c, const char *k, size_t kn, const char *v, size_t vn) {
	int i = 0;
	char* nk = 0;
	char* nv = 0;
	if (c == 0 || kn == 0) {
		return 0;
	}
	for (i = 0; i < c.nenv; i++) {
		if (strlen(c.ekey[i]) == kn && memcmp(c.ekey[i], k, kn) == 0) {
			free(c.eval[i]);
			c.eval[i] = ocmd_strdup(v, vn);
			return c.eval[i] != 0 || vn == 0;
		}
	}
	if (c.nenv >= OsEnvMax) {
		return 0;
	}
	nk = ocmd_strdup(k, kn);
	nv = ocmd_strdup(v, vn);
	if (nk == 0 || (vn != 0 && nv == 0)) {
		free(nk);
		free(nv);
		return 0;
	}
	c.ekey[c.nenv] = nk;
	c.eval[c.nenv] = nv;
	c.nenv++;
	return 1;
}

int ocmd_stdio(OsCmd* c, int which, int mode) {
	if (c == 0 || which < 0 || which > 2) {
		return 0;
	}
	if (mode != OsIoInherit && mode != OsIoNull) {
		return 0;
	}
	if (c.child_h[which] != 0 && c.child_h[which] != INVALID_HANDLE_VALUE) {
		CloseHandle(c.child_h[which]);
		c.child_h[which] = 0;
	}
	c.mode[which] = mode;
	return 1;
}

int ocmd_pipe(OsCmd* c, int which, HANDLE* parent_h) {
	SECURITY_ATTRIBUTES sa = { 0 };
	HANDLE rd = 0;
	HANDLE wr = 0;
	HANDLE parent = 0;
	HANDLE child = 0;
	if (c == 0 || parent_h == 0 || which < 0 || which > 2) {
		return 0;
	}
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = 1;
	if (!CreatePipe(&rd, &wr, &sa, 0)) {
		return 0;
	}
	if (which == OsStdIn) {
		parent = wr;
		child = rd;
		SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0);
	} else {
		parent = rd;
		child = wr;
		SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0);
	}
	if (c.child_h[which] != 0 && c.child_h[which] != INVALID_HANDLE_VALUE) {
		CloseHandle(c.child_h[which]);
	}
	c.child_h[which] = child;
	c.mode[which] = OsIoPipe;
	*parent_h = parent;
	return 1;
}

int ocmd_path_has_sep(const char *p) {
	size_t i = 0;
	if (p == 0) {
		return 0;
	}
	for (i = 0; p[i]; i++) {
		if (p[i] == '/' || p[i] == '\\' || p[i] == ':') {
			return 1;
		}
	}
	return 0;
}

int ocmd_resolve_path(const char *name, char *dst, size_t dstcap) {
	DWORD n = 0;
	if (name == 0 || name[0] == 0 || dst == 0 || dstcap < 2) {
		return 0;
	}
	if (ocmd_path_has_sep(name)) {
		size_t nlen = strlen(name);
		if (nlen + 1 > dstcap) {
			return 0;
		}
		memcpy(dst, name, nlen + 1);
		return 1;
	}
	n = SearchPathA(0, name, ".exe", (DWORD)dstcap, dst, 0);
	if (n > 0 && n < dstcap) {
		return 1;
	}
	n = SearchPathA(0, name, 0, (DWORD)dstcap, dst, 0);
	if (n > 0 && n < dstcap) {
		return 1;
	}
	return 0;
}

/* Quote one argv element for CreateProcess command line. */
int ocmd_quote_arg(const char *arg, char *dst, size_t cap, size_t* used) {
	size_t i = 0;
	size_t o = 0;
	int need = 0;
	if (arg == 0 || dst == 0 || used == 0) {
		return 0;
	}
	o = *used;
	for (i = 0; arg[i]; i++) {
		if (arg[i] == ' ' || arg[i] == '\t' || arg[i] == '"') {
			need = 1;
			break;
		}
	}
	if (!need && arg[0] == 0) {
		need = 1;
	}
	if (need) {
		if (o + 1 >= cap) {
			return 0;
		}
		dst[o++] = '"';
	}
	for (i = 0; arg[i]; i++) {
		if (arg[i] == '"') {
			if (o + 2 >= cap) {
				return 0;
			}
			dst[o++] = '\\';
			dst[o++] = '"';
		} else {
			if (o + 1 >= cap) {
				return 0;
			}
			dst[o++] = arg[i];
		}
	}
	if (need) {
		if (o + 1 >= cap) {
			return 0;
		}
		dst[o++] = '"';
	}
	*used = o;
	return 1;
}

int ocmd_build_cmdline(OsCmd* c, char* dst, size_t cap) {
	size_t used = 0;
	int i = 0;
	if (c == 0 || dst == 0 || cap == 0) {
		return 0;
	}
	for (i = 0; i < c.nargs; i++) {
		if (i > 0) {
			if (used + 1 >= cap) {
				return 0;
			}
			dst[used++] = ' ';
		}
		if (!ocmd_quote_arg(c.argv[i], dst, cap, &used)) {
			return 0;
		}
	}
	if (used >= cap) {
		return 0;
	}
	dst[used] = 0;
	return 1;
}

char* ocmd_build_env_block(OsCmd* c) {
	char* parent = 0;
	char* p = 0;
	char* block = 0;
	size_t cap = 0;
	size_t len = 0;
	int i = 0;
	if (c == 0 || c.nenv == 0) {
		return 0;
	}
	parent = GetEnvironmentStringsA();
	if (parent == 0) {
		return 0;
	}
	/* Measure parent, skipping keys we override or unset. */
	cap = 1;
	p = parent;
	while (*p) {
		char* eq = 0;
		size_t kn = 0;
		int skip = 0;
		int k = 0;
		eq = strchr(p, '=');
		if (eq == 0) {
			p += strlen(p) + 1;
			continue;
		}
		kn = eq - p;
		for (k = 0; k < c.nenv; k++) {
			if (strlen(c.ekey[k]) == kn && memcmp(c.ekey[k], p, kn) == 0) {
				skip = 1;
				break;
			}
		}
		if (!skip) {
			cap += strlen(p) + 1;
		}
		p += strlen(p) + 1;
	}
	for (i = 0; i < c.nenv; i++) {
		if (c.eval[i] == 0 || c.eval[i][0] == 0) {
			continue;
		}
		cap += strlen(c.ekey[i]) + 1 + strlen(c.eval[i]) + 1;
	}
	block = (char*)malloc(cap);
	if (block == 0) {
		FreeEnvironmentStringsA(parent);
		return 0;
	}
	len = 0;
	p = parent;
	while (*p) {
		char* eq = 0;
		size_t kn = 0;
		size_t ln = 0;
		int skip = 0;
		int k = 0;
		eq = strchr(p, '=');
		if (eq == 0) {
			p += strlen(p) + 1;
			continue;
		}
		kn = eq - p;
		for (k = 0; k < c.nenv; k++) {
			if (strlen(c.ekey[k]) == kn && memcmp(c.ekey[k], p, kn) == 0) {
				skip = 1;
				break;
			}
		}
		ln = strlen(p) + 1;
		if (!skip) {
			memcpy(block + len, p, ln);
			len += ln;
		}
		p += ln;
	}
	for (i = 0; i < c.nenv; i++) {
		size_t kn = 0;
		size_t vn = 0;
		if (c.eval[i] == 0 || c.eval[i][0] == 0) {
			continue;
		}
		kn = strlen(c.ekey[i]);
		vn = strlen(c.eval[i]);
		memcpy(block + len, c.ekey[i], kn);
		len += kn;
		block[len++] = '=';
		memcpy(block + len, c.eval[i], vn);
		len += vn;
		block[len++] = 0;
	}
	block[len] = 0;
	FreeEnvironmentStringsA(parent);
	return block;
}

HANDLE ocmd_open_null(int wr) {
	SECURITY_ATTRIBUTES sa = { 0 };
	HANDLE h = 0;
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = 1;
	h = CreateFileA("NUL", wr ? GENERIC_WRITE: GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		return 0;
	}
	return h;
}

int ocmd_start(OsCmd* c) {
	char resolved[OsPathMax] = { 0 };
	char cmdline[OsPathMax * 2] = { 0 };
	STARTUPINFOA si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	HANDLE nul_h[3] = { 0 };
	char* envblock = 0;
	int i = 0;
	BOOL ok = 0;
	if (c == 0 || c.file == 0 || c.started) {
		return 0;
	}
	if (!ocmd_resolve_path(c.file, resolved, sizeof(resolved))) {
		return 0;
	}
	/* Rebuild argv[0] path for quoting. */
	free(c.argv[0]);
	c.argv[0] = ocmd_strdup(resolved, strlen(resolved));
	if (c.argv[0] == 0) {
		return 0;
	}
	if (!ocmd_build_cmdline(c, cmdline, sizeof(cmdline))) {
		return 0;
	}
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	for (i = 0; i < 3; i++) {
		if (c.mode[i] == OsIoPipe) {
			if (c.child_h[i] == 0 || c.child_h[i] == INVALID_HANDLE_VALUE) {
				return 0;
			}
			if (i == 0) {
				si.hStdInput = c.child_h[i];
			} else if (i == 1) {
				si.hStdOutput = c.child_h[i];
			} else {
				si.hStdError = c.child_h[i];
			}
		} else if (c.mode[i] == OsIoNull) {
			nul_h[i] = ocmd_open_null(i != 0);
			if (nul_h[i] == 0) {
				goto fail;
			}
			if (i == 0) {
				si.hStdInput = nul_h[i];
			} else if (i == 1) {
				si.hStdOutput = nul_h[i];
			} else {
				si.hStdError = nul_h[i];
			}
		} else {
			if (i == 0) {
				si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			} else if (i == 1) {
				si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			} else {
				si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
			}
		}
	}
	envblock = ocmd_build_env_block(c);
	ok = CreateProcessA(resolved, cmdline, 0, 0, 1, 0, envblock, c.dir && c.dir[0] ? c.dir: 0, &si, &pi);
	free(envblock);
	for (i = 0; i < 3; i++) {
		if (nul_h[i]) {
			CloseHandle(nul_h[i]);
			nul_h[i] = 0;
		}
		if (c.child_h[i] != 0 && c.child_h[i] != INVALID_HANDLE_VALUE) {
			CloseHandle(c.child_h[i]);
			c.child_h[i] = 0;
		}
	}
	if (!ok) {
		return 0;
	}
	c.proc = pi.hProcess;
	c.thread = pi.hThread;
	c.started = 1;
	c.reaped = 0;
	return 1;
	fail: for (i = 0; i < 3; i++) {
		if (nul_h[i]) {
			CloseHandle(nul_h[i]);
		}
	}
	return 0;
}

int ocmd_wait(OsCmd* c, int* code) {
	DWORD st = 0;
	if (c == 0 || code == 0) {
		return 0;
	}
	if (!c.started) {
		return 0;
	}
	if (c.reaped) {
		*code = c.status;
		return 1;
	}
	if (WaitForSingleObject(c.proc, INFINITE) != WAIT_OBJECT_0) {
		return 0;
	}
	if (!GetExitCodeProcess(c.proc, &st)) {
		return 0;
	}
	c.reaped = 1;
	c.status = st;
	*code = c.status;
	return 1;
}

int ocmd_poll_reap(OsCmd* c) {
	DWORD st = 0;
	DWORD w = 0;
	if (c == 0 || !c.started || c.reaped) {
		return c && c.reaped;
	}
	w = WaitForSingleObject(c.proc, 0);
	if (w == WAIT_TIMEOUT) {
		return 0;
	}
	if (w != WAIT_OBJECT_0) {
		return 0;
	}
	if (!GetExitCodeProcess(c.proc, &st)) {
		return 0;
	}
	c.reaped = 1;
	c.status = st;
	return 1;
}

void ocmd_close(OsCmd* c) {
	if (c == 0) {
		return;
	}
	if (c.started && !c.reaped) {
		TerminateProcess(c.proc, 1);
		WaitForSingleObject(c.proc, INFINITE);
		c.reaped = 1;
		c.status = -1;
	}
	ocmd_clear_stdio(c);
	if (c.proc) {
		CloseHandle(c.proc);
		c.proc = 0;
	}
	if (c.thread) {
		CloseHandle(c.thread);
		c.thread = 0;
	}
	c.started = 0;
}

void opoll_init(OsPoll* p) {
	memset(p, 0, sizeof(*p));
}

int opoll_add_file(OsPoll* p, void* file, int events) {
	int i = 0;
	if (p == 0 || file == 0 || p.n >= OsPollMax) {
		return 0;
	}
	for (i = 0; i < p.n; i++) {
		if (!p.slot[i].is_cmd && p.slot[i].file == file) {
			return 0;
		}
	}
	p.slot[p.n].is_cmd = 0;
	p.slot[p.n].file = file;
	p.slot[p.n].cmd = 0;
	p.slot[p.n].ocmd = 0;
	p.slot[p.n].events = events;
	p.slot[p.n].revents = 0;
	p.slot[p.n].ready = 0;
	p.n++;
	return 1;
}

int opoll_add_cmd(OsPoll* p, void* cmd_handle, void* ocmd) {
	int i = 0;
	if (p == 0 || cmd_handle == 0 || ocmd == 0 || p.n >= OsPollMax) {
		return 0;
	}
	for (i = 0; i < p.n; i++) {
		if (p.slot[i].is_cmd && p.slot[i].cmd == cmd_handle) {
			return 0;
		}
	}
	p.slot[p.n].is_cmd = 1;
	p.slot[p.n].file = 0;
	p.slot[p.n].cmd = cmd_handle;
	p.slot[p.n].ocmd = ocmd;
	p.slot[p.n].events = 0;
	p.slot[p.n].revents = 0;
	p.slot[p.n].ready = 0;
	p.n++;
	return 1;
}

void opoll_reset(OsPoll* p) {
	if (p == 0) {
		return;
	}
	p.n = 0;
	p.nready = 0;
}

/* hs[i] = file HANDLE for file slots; ignored for cmds. */
int opoll_wait(OsPoll* p, HANDLE* hs, int timeout_ms) {
	int i = 0;
	int nready = 0;
	int elapsed = 0;
	DWORD slice = 10;
	if (p == 0) {
		return -1;
	}
	p.nready = 0;
	for (i = 0; i < p.n; i++) {
		p.slot[i].ready = 0;
		p.slot[i].revents = 0;
	}
	for (;;) {
		nready = 0;
		for (i = 0; i < p.n; i++) {
			if (p.slot[i].is_cmd) {
				OsCmd* c = (OsCmd *)p.slot[i].ocmd;
				if (ocmd_poll_reap(c)) {
					p.slot[i].ready = 1;
					p.ready_idx[nready++] = i;
				}
			} else {
				HANDLE h = hs ? hs[i]: 0;
				DWORD avail = 0;
				if (h == 0 || h == INVALID_HANDLE_VALUE) {
					continue;
				}
				if ((p.slot[i].events & 1) != 0) {
					/* PollIn */
					if (PeekNamedPipe(h, 0, 0, 0, &avail, 0)) {
						if (avail > 0) {
							p.slot[i].ready = 1;
							p.slot[i].revents |= 1;
							p.ready_idx[nready++] = i;
						}
					} else {
						/* closed / error → treat as Hup|Err readable */
						p.slot[i].ready = 1;
						p.slot[i].revents |= 4 | 8;
						p.ready_idx[nready++] = i;
					}
				}
			}
		}
		if (nready > 0) {
			p.nready = nready;
			return nready;
		}
		if (timeout_ms == 0) {
			p.nready = 0;
			return 0;
		}
		if (timeout_ms > 0 && elapsed >= timeout_ms) {
			p.nready = 0;
			return 0;
		}
		Sleep(slice);
		if (timeout_ms > 0) {
			elapsed += slice;
		}
	}
}
#endif
