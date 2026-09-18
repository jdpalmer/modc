/*
 * POSIX <fcntl.h> for %C — declaration-only; host libc at link.
 * Flag values are platform-specific (Darwin vs Linux).
 */
#pragma once

#include <sys/types.h>

#ifdef __APPLE__
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_APPEND 0x0008
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_CLOEXEC 0x01000000
#else
/* Linux (and other ELF) traditional bits. */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_CLOEXEC 0x80000
#endif

#define F_SETFD 2
#define FD_CLOEXEC 1

int open(const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);
