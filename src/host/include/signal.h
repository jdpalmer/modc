/*
 * POSIX <signal.h> for %C — curated subset; host libc at link.
 */
#pragma once

#include <sys/types.h>

#define SIGKILL 9

int kill(pid_t pid, int sig);
