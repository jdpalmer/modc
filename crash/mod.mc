// crash — process-fatal funnel (no recover).
//
// crash_init registers an optional cleanup (e.g. tty restore) and installs
// cross-platform fault handlers. panic / __modc_* print a message, run
// cleanups LIFO, then abort. Bounds checks (--bounds-check) and <assert.h>
// call into the same path.
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
	int i = 0;
	for (i = crash_ncleanups - 1; i >= 0; i--) {
		if (crash_cleanups[i] != NULL) {
			crash_cleanups[i]();
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
	if (info == NULL || info.ExceptionRecord == NULL) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	code = info.ExceptionRecord.ExceptionCode;
	if (code == EXCEPTION_ACCESS_VIOLATION || code == 0xc0000374 ||
	    code == 0xc0000409 || code == EXCEPTION_ILLEGAL_INSTRUCTION) {
		if (crash_reporting == 0) {
			crash_reporting = 1;
			crash_run_cleanups();
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void crash_on_signal(int sig) {
	if (crash_reporting == 0) {
		crash_reporting = 1;
		crash_run_cleanups();
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
	char buf[CrashMsgCap] = { 0 };
	int n = 0;
	if (crash_reporting == 0) {
		crash_reporting = 1;
		crash_run_cleanups();
		if (file != NULL && file[0] != 0 && line > 0) {
			n = snprintf(buf, CrashMsgCap, "%s:%d: %s\n", file, line,
				     msg != NULL ? msg : "fatal");
		} else {
			n = snprintf(buf, CrashMsgCap, "panic: %s\n",
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
	char buf[CrashMsgCap] = { 0 };
	int n = 0;
	size_t ix = index;
	size_t ln = len;
	if (crash_reporting == 0) {
		crash_reporting = 1;
		crash_run_cleanups();
		if (file != NULL && file[0] != 0 && line > 0) {
			n = snprintf(buf, CrashMsgCap,
				     "%s:%d: bounds: index %zu >= len %zu\n", file, line, ix, ln);
		} else {
			n = snprintf(buf, CrashMsgCap, "bounds: index %zu >= len %zu\n", ix, ln);
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
	char buf[CrashMsgCap] = { 0 };
	int n = 0;
	if (crash_reporting == 0) {
		crash_reporting = 1;
		crash_run_cleanups();
		n = snprintf(buf, CrashMsgCap, "%s:%d: assert failed: %s\n",
			     file != NULL ? file : "?", line,
			     expr != NULL ? expr : "?");
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
	if (n > 0 && ptr(msg) != NULL) {
		memcpy(buf, ptr(msg), n);
	}
	buf[n] = 0;
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
