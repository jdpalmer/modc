/*
 * POSIX <signal.h> for %C — curated subset; host libc at link.
 */
#pragma once

#include <sys/types.h>

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

#define SIGKILL 9
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#ifdef __APPLE__
#define SIGBUS 10
#else
#define SIGBUS 7
#endif

int kill(pid_t pid, int sig);
int raise(int sig);
sighandler_t signal(int sig, sighandler_t handler);
