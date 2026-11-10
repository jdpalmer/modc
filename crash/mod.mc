// crash — process-fatal funnel (no recover).
//
// crash_init registers an optional cleanup (e.g. tty restore) and installs
// cross-platform fault handlers. panic / __modc_* / fault signals: run cleanups
// LIFO, write a one-line stderr message, then abort (or re-raise the fault).
// Runtime checks (default; omit with --release) and <assert.h> share this path.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#endif

enum { CrashMaxCleanups = 8 };
enum { CrashMsgCap = 512 };

typedef void (*CrashCleanup)();

static CrashCleanup crash_cleanups[CrashMaxCleanups];
static int crash_ncleanups = 0;
static int crash_faults_installed = 0;
static int crash_reporting = 0;

static void crash_write(const char* p, size_t n) {
	if (p == NULL || n == 0) {
		return;
	}
#ifdef _WIN32
	{
		HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
		DWORD w = 0;
		if (h != NULL && h != INVALID_HANDLE_VALUE) {
			(void)WriteFile(h, p, (DWORD)n, &w, NULL);
		}
	}
#else
	(void)write(STDERR_FILENO, p, n);
#endif
}

static void crash_run_cleanups() {
	int i = crash_ncleanups - 1;
	for (; i >= 0; i--) {
		if (crash_cleanups[i] != NULL) {
			crash_cleanups[i]();
		}
	}
}

/* Cleanup first (tty restore), then message — same order as __modc_*. */
static void crash_begin_report(const char* msg) {
	if (crash_reporting != 0) {
		return;
	}
	crash_reporting = 1;
	crash_run_cleanups();
	if (msg == NULL || *msg == 0) {
		return;
	}
	{
		char buf[CrashMsgCap] = { 0 };
		int n = snprintf(buf, CrashMsgCap, "%s", msg);
		if (n > 0) {
			if (n >= CrashMsgCap) {
				n = CrashMsgCap - 1;
			}
			crash_write(buf, n);
		}
	}
}

static void crash_die() {
#ifdef _WIN32
	abort();
#else
	signal(SIGABRT, SIG_DFL);
	abort();
#endif
}

#ifdef _WIN32
static LONG crash_vectored(EXCEPTION_POINTERS* info) {
	DWORD code = 0;
	const char* msg = "crash: fault\n";
	if (info == NULL || info.ExceptionRecord == NULL) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	code = info.ExceptionRecord.ExceptionCode;
	if (code == EXCEPTION_ACCESS_VIOLATION) {
		msg = "crash: access violation\n";
	} else if (code == 0xc0000374) {
		msg = "crash: heap corruption\n";
	} else if (code == 0xc0000409) {
		msg = "crash: stack buffer overrun\n";
	} else if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
		msg = "crash: illegal instruction\n";
	} else {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	crash_begin_report(msg);
	return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void crash_on_signal(int sig) {
	const char* name = "signal";

	if (sig == SIGSEGV) {
		name = "SIGSEGV";
	} else if (sig == SIGABRT) {
		name = "SIGABRT";
	} else if (sig == SIGFPE) {
		name = "SIGFPE";
#ifdef SIGBUS
	} else if (sig == SIGBUS) {
		name = "SIGBUS";
#endif
	}
	{
		char buf[CrashMsgCap] = { 0 };
		int n = snprintf(buf, CrashMsgCap, "crash: %s\n", name);
		if (n <= 0 || n >= CrashMsgCap) {
			crash_begin_report("crash: signal\n");
		} else {
			crash_begin_report(buf);
		}
	}
	signal(sig, SIG_DFL);
	raise(sig);
}
#endif

static void crash_install_faults() {
	if (crash_faults_installed) {
		return;
	}
	crash_faults_installed = 1;
#ifdef _WIN32
	(void)AddVectoredExceptionHandler(1, crash_vectored);
#else
	signal(SIGSEGV, crash_on_signal);
	signal(SIGABRT, crash_on_signal);
	signal(SIGFPE, crash_on_signal);
#ifdef SIGBUS
	signal(SIGBUS, crash_on_signal);
#endif
#endif
}

void crash_init(CrashCleanup fn) {
	if (fn != NULL && crash_ncleanups < CrashMaxCleanups) {
		crash_cleanups[crash_ncleanups] = fn;
		crash_ncleanups++;
	}
	crash_install_faults();
}

void __modc_fatal(const char* file, int line, const char* msg) {
	if (crash_reporting == 0) {
		char buf[CrashMsgCap] = { 0 };
		int n = snprintf(buf, CrashMsgCap, "panic: %s\n",
				 msg != NULL ? msg : "fatal");
		crash_reporting = 1;
		crash_run_cleanups();
		if (file != NULL && *file != 0 && line > 0) {
			n = snprintf(buf, CrashMsgCap, "%s:%d: %s\n", file, line,
				     msg != NULL ? msg : "fatal");
		}
		if (n > 0) {
			if (n >= CrashMsgCap) {
				n = CrashMsgCap - 1;
			}
			crash_write(buf, n);
		}
	}
	crash_die();
}

void __modc_bounds_fail(const char* file, int line, uint64_t index, uint64_t len) {
	size_t ix = index;
	size_t ln = len;
	if (crash_reporting == 0) {
		char buf[CrashMsgCap] = { 0 };
		char hint[96] = { 0 };
		/* index == len is the classic off-by-one (trim, exclusive end, …). */
		if (ix == ln) {
			(void)snprintf(hint, sizeof(hint),
				       " (index == len; off-by-one? valid 0..%zu)",
				       ln > 0 ? ln - 1 : 0);
		}
		crash_reporting = 1;
		crash_run_cleanups();
		{
			int n = (file != NULL && *file != 0 && line > 0)
					? snprintf(buf, CrashMsgCap,
						   "%s:%d: bounds: index %zu >= len %zu%s\n", file,
						   line, ix, ln, hint)
					: snprintf(buf, CrashMsgCap,
						   "bounds: index %zu >= len %zu%s\n", ix, ln, hint);
			if (n > 0) {
				if (n >= CrashMsgCap) {
					n = CrashMsgCap - 1;
				}
				crash_write(buf, n);
			}
		}
	}
	crash_die();
}

void __modc_null_fail(const char* file, int line) {
	if (crash_reporting == 0) {
		char buf[CrashMsgCap] = { 0 };
		int n = snprintf(buf, CrashMsgCap, "null method receiver\n");
		crash_reporting = 1;
		crash_run_cleanups();
		if (file != NULL && *file != 0 && line > 0) {
			n = snprintf(buf, CrashMsgCap, "%s:%d: null method receiver\n", file,
				     line);
		}
		if (n > 0) {
			if (n >= CrashMsgCap) {
				n = CrashMsgCap - 1;
			}
			crash_write(buf, n);
		}
	}
	crash_die();
}

void __modc_assert_fail(const char* file, int line, const char* expr) {
	if (crash_reporting == 0) {
		char buf[CrashMsgCap] = { 0 };
		int n = snprintf(buf, CrashMsgCap, "%s:%d: assert failed: %s\n",
				 file != NULL ? file : "?", line,
				 expr != NULL ? expr : "?");
		crash_reporting = 1;
		crash_run_cleanups();
		if (n > 0) {
			if (n >= CrashMsgCap) {
				n = CrashMsgCap - 1;
			}
			crash_write(buf, n);
		}
	}
	crash_die();
}

static const char* crash_zmsg(const char[..] msg, char* buf, size_t cap) {
	size_t n = len(msg);
	if (cap == 0) {
		return "";
	}
	if (n >= cap) {
		n = cap - 1;
	}
	{
		void* p = ptr(msg);
		if (n > 0 && p != NULL) {
			memcpy(buf, p, n);
		}
	}
	{
		char[..] room = ranged(buf, cap);
		room[n] = 0;
	}
	return buf;
}

overload void panic(const char[..] msg) {
	char buf[CrashMsgCap] = { 0 };
	__modc_fatal(NULL, 0, crash_zmsg(msg, buf, CrashMsgCap));
}

overload void panic(const char[..] file, int line, const char[..] msg) {
	char fbuf[CrashMsgCap] = { 0 };
	char mbuf[CrashMsgCap] = { 0 };
	__modc_fatal(crash_zmsg(file, fbuf, CrashMsgCap), line,
		     crash_zmsg(msg, mbuf, CrashMsgCap));
}

/* ---- Pool / slice runtime (language builtins lower to these) ----
 * Layout must match compiler Pool and T[..] ({ptr,len,cap}). */
typedef struct {
	void* ptr;
	size_t len;
	size_t cap;
} __modc_slice;

typedef struct {
	char* base;
	size_t cap;
	size_t off;
} __modc_pool;

static bool __modc_pool_ensure(__modc_pool* a, size_t need) {
	if (a == NULL) {
		return false;
	}
	if (need <= a.cap) {
		return true;
	}
	size_t newcap = 4096;
	if (a.cap != 0) {
		newcap = a.cap;
	}
	while (newcap < need) {
		newcap = newcap * 2;
	}
	char* p = realloc(a.base, newcap);
	if (p == NULL) {
		return false;
	}
	a.base = p;
	a.cap = newcap;
	return true;
}

static char* __modc_pool_bump(__modc_pool* a, size_t n) {
	if (a == NULL || n == 0) {
		return NULL;
	}
	if (!__modc_pool_ensure(a, a.off + n)) {
		return NULL;
	}
	char* p = a.base + a.off;
	a.off = a.off + n;
	return p;
}

static size_t __modc_align_up(size_t off, size_t align) {
	if (align < 1) {
		align = 1;
	}
	return (off + align - 1) & ~(align - 1);
}

static char* __modc_pool_bump_aligned(__modc_pool* a, size_t n, size_t align) {
	if (a == NULL || n == 0) {
		return NULL;
	}
	size_t off = __modc_align_up(a.off, align);
	if (!__modc_pool_ensure(a, off + n)) {
		return NULL;
	}
	char* p = a.base + off;
	a.off = off + n;
	return p;
}

void* __modc_alloc(size_t elemsz) {
	if (elemsz == 0) {
		return NULL;
	}
	return malloc(elemsz);
}

void __modc_alloc_slice(__modc_slice* out, size_t elemsz, size_t n) {
	if (out == NULL) {
		return;
	}
	out.ptr = NULL;
	out.len = 0;
	out.cap = 0;
	if (elemsz == 0 || n == 0) {
		return;
	}
	if (n > (size_t) - 1 / elemsz) {
		return;
	}
	void* p = malloc(n * elemsz);
	if (p == NULL) {
		return;
	}
	out.ptr = p;
	out.len = n;
	out.cap = n;
}

bool __modc_reserve(__modc_slice* s, size_t elemsz, size_t need) {
	if (s == NULL || elemsz == 0) {
		return false;
	}
	if (need <= s.cap) {
		return true;
	}
	size_t nc = s.cap;
	if (nc == 0) {
		nc = 8;
	}
	while (nc < need) {
		if (nc > (size_t) - 1 / 2) {
			nc = need;
			break;
		}
		nc = nc * 2;
	}
	if (nc < need) {
		nc = need;
	}
	if (nc > (size_t) - 1 / elemsz) {
		return false;
	}
	void* p = realloc(s.ptr, nc * elemsz);
	if (p == NULL) {
		return false;
	}
	s.ptr = p;
	s.cap = nc;
	return true;
}

bool __modc_push(__modc_slice* s, size_t elemsz, void* elem) {
	if (s == NULL || elemsz == 0 || elem == NULL) {
		return false;
	}
	size_t n = s.len;
	if (n + 1 < n) {
		return false;
	}
	if (!__modc_reserve(s, elemsz, n + 1)) {
		return false;
	}
	memcpy((char*)s.ptr + n * elemsz, elem, elemsz);
	s.len = n + 1;
	return true;
}

bool __modc_append(__modc_slice* s, size_t elemsz, __modc_slice* extra) {
	if (s == NULL || elemsz == 0) {
		return false;
	}
	if (extra == NULL || extra.len == 0) {
		return true;
	}
	size_t n = s.len;
	size_t m = extra.len;
	size_t newlen = n + m;
	if (newlen < n) {
		return false;
	}
	if (!__modc_reserve(s, elemsz, newlen)) {
		return false;
	}
	memmove((char*)s.ptr + n * elemsz, extra.ptr, m * elemsz);
	s.len = newlen;
	return true;
}

void __modc_release(__modc_slice* s) {
	if (s == NULL) {
		return;
	}
	if (s.ptr != NULL) {
		free(s.ptr);
	}
	s.ptr = NULL;
	s.len = 0;
	s.cap = 0;
}

void __modc_pool_free(__modc_pool* p) {
	if (p == NULL) {
		return;
	}
	free(p.base);
	p.base = NULL;
	p.cap = 0;
	p.off = 0;
}

void __modc_pool_reset(__modc_pool* p) {
	if (p == NULL) {
		return;
	}
	p.off = 0;
}

bool __modc_pool_copy(__modc_pool* p, __modc_slice* dst, size_t elemsz, __modc_slice* src) {
	if (p == NULL || dst == NULL || elemsz == 0) {
		return false;
	}
	size_t n = src ? src.len : 0;
	if (n == 0) {
		dst.ptr = NULL;
		dst.len = 0;
		dst.cap = 0;
		return true;
	}
	if (n > (size_t) - 1 / elemsz) {
		return false;
	}
	char* mem = __modc_pool_bump(p, n * elemsz);
	if (mem == NULL) {
		return false;
	}
	memcpy(mem, src.ptr, n * elemsz);
	dst.ptr = mem;
	dst.len = n;
	dst.cap = n;
	return true;
}

bool __modc_pool_append(__modc_pool* p, __modc_slice* s, size_t elemsz, __modc_slice* extra) {
	if (p == NULL || s == NULL || elemsz == 0) {
		return false;
	}
	size_t cn = s.len;
	size_t n = extra ? extra.len : 0;
	size_t newlen = cn + n;
	if (newlen < cn) {
		return false;
	}
	if (newlen == 0) {
		s.ptr = NULL;
		s.len = 0;
		s.cap = 0;
		return true;
	}
	if (n == 0) {
		return true;
	}
	if (newlen > (size_t) - 1 / elemsz) {
		return false;
	}
	char* mem = __modc_pool_bump(p, newlen * elemsz);
	if (mem == NULL) {
		return false;
	}
	if (cn != 0) {
		memcpy(mem, s.ptr, cn * elemsz);
	}
	memcpy(mem + cn * elemsz, extra.ptr, n * elemsz);
	s.ptr = mem;
	s.len = newlen;
	s.cap = newlen;
	return true;
}

bool __modc_pool_push(__modc_pool* p, __modc_slice* s, size_t elemsz, void* elem) {
	if (elem == NULL) {
		return false;
	}
	__modc_slice one = { 0 };
	one.ptr = elem;
	one.len = 1;
	one.cap = 1;
	return __modc_pool_append(p, s, elemsz, &one);
}

void* __modc_pool_alloc_ptr(__modc_pool* p, size_t elemsz, size_t align) {
	if (p == NULL || elemsz == 0) {
		return NULL;
	}
	return __modc_pool_bump_aligned(p, elemsz, align);
}

void __modc_pool_alloc_slice(__modc_pool* p, __modc_slice* out, size_t elemsz, size_t n,
			     size_t align) {
	if (out == NULL) {
		return;
	}
	out.ptr = NULL;
	out.len = 0;
	out.cap = 0;
	if (p == NULL || elemsz == 0 || n == 0) {
		return;
	}
	if (n > (size_t) - 1 / elemsz) {
		return;
	}
	char* mem = __modc_pool_bump_aligned(p, n * elemsz, align);
	if (mem == NULL) {
		return;
	}
	out.ptr = mem;
	out.len = n;
	out.cap = n;
}

bool __modc_pool_join(__modc_pool* p, __modc_slice* dst, __modc_slice* sep, __modc_slice* parts,
		      size_t nparts) {
	if (p == NULL || dst == NULL) {
		return false;
	}
	dst.ptr = NULL;
	dst.len = 0;
	dst.cap = 0;
	__modc_slice[..] ps = { 0 };
	if (parts != NULL && nparts != 0) {
		ps = ranged(parts, nparts);
	}
	for (size_t i = 0; i < nparts; i++) {
		__modc_slice one = { 0 };
		if (i != 0) {
			if (!__modc_pool_append(p, dst, 1, sep)) {
				return false;
			}
		}
		one = ps[i];
		if (!__modc_pool_append(p, dst, 1, &one)) {
			return false;
		}
	}
	return true;
}

bool __modc_pool_replace(__modc_pool* p, __modc_slice* dst, __modc_slice* src, __modc_slice* old,
			 __modc_slice* neu) {
	if (p == NULL || dst == NULL || src == NULL) {
		return false;
	}
	size_t olen = old ? old.len : 0;
	if (olen == 0) {
		return __modc_pool_copy(p, dst, 1, src);
	}
	dst.ptr = NULL;
	dst.len = 0;
	dst.cap = 0;
	size_t slen = src.len;
	size_t i = 0;
	while (i < slen) {
		int found = 0;
		size_t rem = slen - i;
		char* base = (char*)src.ptr + i;
		size_t hit = 0;
		if (rem >= olen) {
			for (size_t j = 0; j + olen <= rem; j++) {
				if (memcmp(base + j, old.ptr, olen) == 0) {
					hit = j;
					found = 1;
					break;
				}
			}
		}
		if (!found) {
			__modc_slice tail = { 0 };
			tail.ptr = base;
			tail.len = rem;
			tail.cap = rem;
			return __modc_pool_append(p, dst, 1, &tail);
		}
		{
			__modc_slice pre = { 0 };
			pre.ptr = base;
			pre.len = hit;
			pre.cap = hit;
			if (!__modc_pool_append(p, dst, 1, &pre)) {
				return false;
			}
		}
		if (!__modc_pool_append(p, dst, 1, neu)) {
			return false;
		}
		i = i + hit + olen;
	}
	return true;
}

char* __modc_cstr(__modc_slice* s) {
	size_t n = s ? s.len : 0;
	char* mem = malloc(n + 1);
	if (mem == NULL) {
		return NULL;
	}
	if (n != 0 && s != NULL && s.ptr != NULL) {
		memcpy(mem, s.ptr, n);
	}
	{
		char[..] room = ranged(mem, n + 1);
		room[n] = 0;
	}
	return mem;
}

char* __modc_pool_cstr(__modc_pool* p, __modc_slice* s) {
	if (p == NULL) {
		return NULL;
	}
	size_t n = s ? s.len : 0;
	char* mem = __modc_pool_bump(p, n + 1);
	if (mem == NULL) {
		return NULL;
	}
	if (n != 0 && s != NULL && s.ptr != NULL) {
		memcpy(mem, s.ptr, n);
	}
	{
		char[..] room = ranged(mem, n + 1);
		room[n] = 0;
	}
	return mem;
}

int __modc_strcmp_slice(__modc_slice* a, __modc_slice* b) {
	size_t an = a ? a.len : 0;
	size_t bn = b ? b.len : 0;
	size_t n = an < bn ? an : bn;
	int r = 0;
	if (n != 0 && a != NULL && b != NULL && a.ptr != NULL && b.ptr != NULL) {
		r = memcmp(a.ptr, b.ptr, n);
	} else if (n != 0) {
		if (a == NULL || a.ptr == NULL) {
			r = -1;
		} else {
			r = 1;
		}
	}
	if (r != 0) {
		return r;
	}
	if (an < bn) {
		return -1;
	}
	if (an > bn) {
		return 1;
	}
	return 0;
}

int __modc_strncmp_slice(__modc_slice* a, __modc_slice* b, size_t n) {
	size_t an = a ? a.len : 0;
	size_t bn = b ? b.len : 0;
	if (an > n) {
		an = n;
	}
	if (bn > n) {
		bn = n;
	}
	{
		__modc_slice aa = { 0 };
		__modc_slice bb = { 0 };
		aa.ptr = a ? a.ptr : NULL;
		aa.len = an;
		aa.cap = an;
		bb.ptr = b ? b.ptr : NULL;
		bb.len = bn;
		bb.cap = bn;
		return __modc_strcmp_slice(&aa, &bb);
	}
}

int __modc_puts_slice(__modc_slice* s) {
	size_t n = s ? s.len : 0;
	if (n != 0 && s != NULL && s.ptr != NULL) {
		if (fwrite(s.ptr, 1, n, stdout) != n) {
			return EOF;
		}
	}
	if (putchar('\n') == EOF) {
		return EOF;
	}
	return 0;
}

/* main(const char[..][..] args) sugar: views into argv (NUL excluded from len/cap).
 * Allocates the outer header array once; lives for the process. */
void __modc_main_args(int argc, char** argv, __modc_slice* out) {
	if (out == NULL) {
		return;
	}
	out.ptr = NULL;
	out.len = 0;
	out.cap = 0;
	if (argc <= 0) {
		return;
	}
	{
		size_t n = argc;
		__modc_slice* hdrs = (__modc_slice*)calloc(n, sizeof(__modc_slice));
		char** p = argv;
		size_t i = 0;

		if (hdrs == NULL) {
			return;
		}
		{
			__modc_slice[..] hs = ranged(hdrs, n);

			for (; i < n; i++) {
				char* s = NULL;
				size_t ln = 0;

				if (p != NULL) {
					s = *p;
					p++;
				}
				if (s != NULL) {
					ln = strlen(s);
				}
				hs[i].ptr = s;
				hs[i].len = ln;
				hs[i].cap = ln;
			}
		}
		out.ptr = (char*)hdrs;
		out.len = n;
		out.cap = n;
	}
}
