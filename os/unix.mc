// POSIX helpers for os (package-internal).
#ifndef _WIN32
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <stddef.h>

/* Unix Cmd / Poll helpers for os/mod.mc. */

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
	int child_fd[3];
	int pid;
	int started;
	int reaped;
	int status;
};

struct OsPollSlot {
	int is_cmd;
	void* file;
	/* File* */
	void* cmd;
	/* Cmd* handle */
	void* ocmd;
	/* OsCmd* */
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
		if (c.child_fd[i] >= 0) {
			close(c.child_fd[i]);
			c.child_fd[i] = -1;
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
		c.child_fd[i] = -1;
	}
	c.pid = -1;
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
	if (c.child_fd[which] >= 0) {
		close(c.child_fd[which]);
		c.child_fd[which] = -1;
	}
	c.mode[which] = mode;
	return 1;
}

/* Returns parent fd via *parent_fd; child stored on cmd. Caller wraps parent. */
int ocmd_pipe(OsCmd* c, int which, int* parent_fd) {
	int pfd[2] = { 0 };
	int parent = 0;
	int child = 0;
	if (c == 0 || parent_fd == 0 || which < 0 || which > 2) {
		return 0;
	}
	if (pipe(pfd) != 0) {
		return 0;
	}
	fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
	fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
	if (which == OsStdIn) {
		parent = pfd[1];
		child = pfd[0];
	} else {
		parent = pfd[0];
		child = pfd[1];
	}
	if (c.child_fd[which] >= 0) {
		close(c.child_fd[which]);
	}
	c.child_fd[which] = child;
	c.mode[which] = OsIoPipe;
	*parent_fd = parent;
	return 1;
}

int ocmd_path_is_abs(const char *p) {
	return p != 0 && p[0] == '/';
}

int ocmd_resolve_path(const char *name, char *dst, size_t dstcap) {
	const char *path = 0;
	const char *a = 0;
	const char *b = 0;
	size_t nlen = 0;
	size_t plen = 0;
	size_t tot = 0;
	if (name == 0 || name[0] == 0 || dst == 0 || dstcap < 2) {
		return 0;
	}
	nlen = strlen(name);
	if (ocmd_path_is_abs(name) || strchr(name, '/') != 0) {
		if (nlen + 1 > dstcap) {
			return 0;
		}
		memcpy(dst, name, nlen + 1);
		return 1;
	}
	path = getenv("PATH");
	if (path == 0) {
		path = "/usr/bin:/bin";
	}
	a = path;
	while (*a) {
		b = a;
		while (*b &&*b != ':') {
			b++;
		}
		plen = b - a;
		if (plen == 0) {
			a = *b ? b + 1: b;
			continue;
		}
		tot = plen + 1 + nlen;
		if (tot + 1 > dstcap) {
			a = *b ? b + 1: b;
			continue;
		}
		memcpy(dst, a, plen);
		dst[plen] = '/';
		memcpy(dst + plen + 1, name, nlen);
		dst[tot] = 0;
		{
			int fd = open(dst, O_RDONLY);
			if (fd >= 0) {
				close(fd);
				return 1;
			}
		}
		a = *b ? b + 1: b;
	}
	return 0;
}

char** ocmd_build_env(OsCmd* c) {
	char** env = 0;
	int n = 0;
	int i = 0;
	int j = 0;
	int cap = 0;
	char** e = 0;
	e = environ;
	n = 0;
	if (e) {
		while (e[n]) {
			n++;
		}
	}
	cap = n + c.nenv + 1;
	env = (char**)malloc((size_t)cap * sizeof(char*));
	if (env == 0) {
		return 0;
	}
	j = 0;
	for (i = 0; i < n; i++) {
		char* eq = 0;
		size_t kn = 0;
		int skip = 0;
		int k = 0;
		eq = strchr(e[i], '=');
		if (eq == 0) {
			continue;
		}
		kn = eq - e[i];
		for (k = 0; k < c.nenv; k++) {
			if (strlen(c.ekey[k]) == kn && memcmp(c.ekey[k], e[i], kn) == 0) {
				skip = 1;
				break;
			}
		}
		if (!skip) {
			env[j] = e[i];
			j++;
		}
	}
	for (i = 0; i < c.nenv; i++) {
		size_t kn = 0;
		size_t vn = 0;
		size_t tot = 0;
		char* line = 0;
		if (c.eval[i] == 0 || c.eval[i][0] == 0) {
			/* empty val = unset: already skipped from parent */
			continue;
		}
		kn = strlen(c.ekey[i]);
		vn = strlen(c.eval[i]);
		tot = kn + 1 + vn;
		line = (char*)malloc(tot + 1);
		if (line == 0) {
			/* leak prior env lines we malloc'd — only overlay lines */
			while (--i >= 0) {
				/* can't free parent strings */
			}
			free(env);
			return 0;
		}
		memcpy(line, c.ekey[i], kn);
		line[kn] = '=';
		memcpy(line + kn + 1, c.eval[i], vn);
		line[tot] = 0;
		env[j++] = line;
	}
	env[j] = 0;
	return env;
}

void ocmd_free_envp(char** env, OsCmd* c) {
	int i = 0;
	int nparent = 0;
	if (env == 0) {
		return;
	}
	if (environ) {
		while (environ[nparent]) {
			nparent++;
		}
	}
	/* Overlay lines were malloc'd after parent copies — free any not in environ */
	for (i = 0; env[i]; i++) {
		int from_parent = 0;
		int j = 0;
		for (j = 0; j < nparent; j++) {
			if (env[i] == environ[j]) {
				from_parent = 1;
				break;
			}
		}
		if (!from_parent) {
			free(env[i]);
		}
	}
	free(env);
	(void)c;
}

int ocmd_open_null(int wr) {
	int fd = 0;
	fd = open("/dev/null", wr ? O_WRONLY: O_RDONLY);
	return fd;
}

int ocmd_start(OsCmd* c) {
	char resolved[OsPathMax] = { 0 };
	char** envp = 0;
	int pid = 0;
	int i = 0;
	int nullfd = 0;
	if (c == 0 || c.file == 0 || c.started) {
		return 0;
	}
	if (ocmd_path_is_abs(c.file) || strchr(c.file, '/') != 0) {
		if (strlen(c.file) + 1 > sizeof(resolved)) {
			return 0;
		}
		memcpy(resolved, c.file, strlen(c.file) + 1);
	} else if (!ocmd_resolve_path(c.file, resolved, sizeof(resolved))) {
		return 0;
	}
	envp = ocmd_build_env(c);
	if (envp == 0) {
		return 0;
	}
	pid = fork();
	if (pid < 0) {
		ocmd_free_envp(envp, c);
		return 0;
	}
	if (pid == 0) {
		/* child */
		for (i = 0; i < 3; i++) {
			if (c.mode[i] == OsIoPipe) {
				if (c.child_fd[i] < 0) {
					_exit(127);
				}
				if (dup2(c.child_fd[i], i) < 0) {
					_exit(127);
				}
			} else if (c.mode[i] == OsIoNull) {
				nullfd = ocmd_open_null(i != 0);
				if (nullfd < 0) {
					_exit(127);
				}
				if (dup2(nullfd, i) < 0) {
					_exit(127);
				}
				close(nullfd);
			}
		}
		for (i = 0; i < 3; i++) {
			if (c.child_fd[i] >= 0) {
				close(c.child_fd[i]);
			}
		}
		if (c.dir && c.dir[0]) {
			if (chdir(c.dir) != 0) {
				_exit(127);
			}
		}
		execve(resolved, c.argv, envp);
		_exit(127);
	}
	/* parent */
	ocmd_free_envp(envp, c);
	for (i = 0; i < 3; i++) {
		if (c.child_fd[i] >= 0) {
			close(c.child_fd[i]);
			c.child_fd[i] = -1;
		}
	}
	c.pid = pid;
	c.started = 1;
	c.reaped = 0;
	return 1;
}

int ocmd_status_code(int st) {
	if (WIFEXITED(st)) {
		return WEXITSTATUS(st);
	}
	if (WIFSIGNALED(st)) {
		return 128 + (st & 0x7f);
	}
	return st;
}

int ocmd_wait(OsCmd* c, int* code) {
	int st = 0;
	int rc = 0;
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
	rc = waitpid(c.pid, &st, 0);
	if (rc < 0) {
		return 0;
	}
	c.reaped = 1;
	c.status = ocmd_status_code(st);
	*code = c.status;
	return 1;
}

/* Non-blocking reap for Poll. Returns 1 if exited (reaped). */
int ocmd_poll_reap(OsCmd* c) {
	int st = 0;
	int rc = 0;
	if (c == 0 || !c.started || c.reaped) {
		return c && c.reaped;
	}
	rc = waitpid(c.pid, &st, WNOHANG);
	if (rc == 0) {
		return 0;
	}
	if (rc < 0) {
		return 0;
	}
	c.reaped = 1;
	c.status = ocmd_status_code(st);
	return 1;
}

void ocmd_close(OsCmd* c) {
	if (c == 0) {
		return;
	}
	if (c.started && !c.reaped) {
		kill(c.pid, SIGKILL);
		waitpid(c.pid, 0, 0);
		c.reaped = 1;
		c.status = -1;
	}
	ocmd_clear_stdio(c);
	c.started = 0;
	c.pid = -1;
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

/* fd_of_file: callback provided by mod.mc via... we pass fds array from %C.
   Actually Poll.wait in %C will build pollfd from file_fd. So implement
   opoll_wait that takes fds[] parallel to file slots.

   Simpler: opoll_wait_unix(p, fds, nfds_files_mapping) 

   Even simpler: do the whole wait in %C calling waitpid and poll.

   Keep C helper that:
   - takes OsPoll*
   - takes int fd_for_slot[OsPollMax] (-1 for cmds)
   - returns nready
*/
int opoll_wait(OsPoll* p, int* fds, int timeout_ms) {
	struct pollfd pf[OsPollMax] = { 0 };
	int map[OsPollMax] = { 0 };
	int np = 0;
	int i = 0;
	int rc = 0;
	int nready = 0;
	int only_cmds = 0;
	if (p == 0) {
		return -1;
	}
	p.nready = 0;
	for (i = 0; i < p.n; i++) {
		p.slot[i].ready = 0;
		p.slot[i].revents = 0;
	}
	/* Reap cmds first */
	nready = 0;
	only_cmds = 1;
	for (i = 0; i < p.n; i++) {
		if (p.slot[i].is_cmd) {
			OsCmd* c = (OsCmd *)p.slot[i].ocmd;
			if (ocmd_poll_reap(c)) {
				p.slot[i].ready = 1;
				p.ready_idx[nready++] = i;
			}
		} else {
			only_cmds = 0;
		}
	}
	if (nready > 0 && timeout_ms == 0) {
		p.nready = nready;
		return nready;
	}
	np = 0;
	for (i = 0; i < p.n; i++) {
		if (p.slot[i].is_cmd) {
			continue;
		}
		if (fds[i] < 0) {
			continue;
		}
		pf[np].fd = fds[i];
		pf[np].events = 0;
		if (p.slot[i].events & 1) {
			pf[np].events |= POLLIN;
		}
		if (p.slot[i].events & 2) {
			pf[np].events |= POLLOUT;
		}
		pf[np].revents = 0;
		map[np] = i;
		np++;
	}
	if (np == 0) {
		/* only cmds: block with waitpid if timeout < 0 and none ready */
		if (nready > 0) {
			p.nready = nready;
			return nready;
		}
		if (timeout_ms == 0) {
			p.nready = 0;
			return 0;
		}
		/* blocking: wait on first non-reaped cmd */
		for (i = 0; i < p.n; i++) {
			if (!p.slot[i].is_cmd) {
				continue;
			}
			{
				OsCmd* c = (OsCmd *)p.slot[i].ocmd;
				int st = 0;
				if (c.reaped) {
					continue;
				}
				if (timeout_ms < 0) {
					if (waitpid(c.pid, &st, 0) > 0) {
						c.reaped = 1;
						c.status = ocmd_status_code(st);
						p.slot[i].ready = 1;
						p.ready_idx[0] = i;
						p.nready = 1;
						return 1;
					}
					return -1;
				}
			}
		}
		/* timed wait with only cmds: poll with timeout using a dummy sleep via poll(0) —
		   not portable. Use poll on no fds — Linux allows nfds=0. */
		rc = poll(0, 0, timeout_ms < 0 ? -1: timeout_ms);
		(void)rc;
		for (i = 0; i < p.n; i++) {
			if (p.slot[i].is_cmd && ocmd_poll_reap((OsCmd *)p.slot[i].ocmd)) {
				p.slot[i].ready = 1;
				p.ready_idx[nready++] = i;
			}
		}
		p.nready = nready;
		return nready;
	}
	{
		int t = timeout_ms;
		if (nready > 0) {
			t = 0;
			/* already have cmd ready: don't block */
		}
		rc = poll(pf, np, t);
	}
	if (rc < 0) {
		return -1;
	}
	for (i = 0; i < np; i++) {
		int si = map[i];
		int rev = 0;
		if (pf[i].revents == 0) {
			continue;
		}
		if (pf[i].revents & POLLIN) {
			rev |= 1;
		}
		if (pf[i].revents & POLLOUT) {
			rev |= 2;
		}
		if (pf[i].revents & POLLHUP) {
			rev |= 4;
		}
		if (pf[i].revents & POLLERR) {
			rev |= 8;
		}
		p.slot[si].revents = rev;
		p.slot[si].ready = 1;
		p.ready_idx[nready++] = si;
	}
	/* cmds again after poll */
	for (i = 0; i < p.n; i++) {
		if (!p.slot[i].is_cmd || p.slot[i].ready) {
			continue;
		}
		if (ocmd_poll_reap((OsCmd *)p.slot[i].ocmd)) {
			p.slot[i].ready = 1;
			p.ready_idx[nready++] = i;
		}
	}
	p.nready = nready;
	return nready;
}
#endif
