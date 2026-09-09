/*
 * C <errno.h> for %C — errno access + common macros; host libc at link.
 * E* numeric values are host-specific (Apple vs Linux diverge on some codes).
 */
#pragma once

#ifdef __APPLE__
int *__error(void);
#define errno (*__error())
#elif defined(__linux__)
int *__errno_location(void);
#define errno (*__errno_location())
#elif defined(_WIN32)
int *_errno(void);
#define errno (*_errno())
#else
extern int errno;
#endif

/* ISO C */
#ifdef __APPLE__
#define EDOM   33
#define EILSEQ 92
#define ERANGE 34
#elif defined(__linux__)
#define EDOM   33
#define EILSEQ 84
#define ERANGE 34
#else
#define EDOM   33
#define EILSEQ 84
#define ERANGE 34
#endif

/* Common POSIX (same numbers on Apple and Linux unless noted) */
#define EPERM		1
#define ENOENT		2
#define EINTR		4
#define EIO		5
#define ENOMEM		12
#define EACCES		13
#define EEXIST		17
#define ENOTDIR		20
#define EISDIR		21
#define EINVAL		22
#define ENOSPC		28
#define EPIPE		32

#ifdef __APPLE__
#define EAGAIN		35
#else
#define EAGAIN		11
#endif
#define EWOULDBLOCK	EAGAIN
