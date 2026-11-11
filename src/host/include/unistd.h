/*
 * POSIX <unistd.h> for %C — declaration-only; host libc at link.
 * Curated subset for fs/os. Does not pull Darwin fd-set inlines.
 */
#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);

int unlink(const char *path);
int rmdir(const char *path);

char *getcwd(char *buf, size_t size);
int chdir(const char *path);

int pipe(int fd[2]);
int dup2(int oldfd, int newfd);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
void _exit(int status);

int isatty(int fd);
pid_t getpid(void);

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

#ifdef _WIN32
/* PE: environ is not dllimport data (same class as stdin/err); UCRT accessor.
 * Deferred if ever stubbed: _fmode, _pgmptr, _doserrno, _sys_nerr/_sys_errlist,
 * daylight/timezone/tzname — see docs/interop.md. */
char ***__p__environ(void);
#define environ (*__p__environ())
#else
extern char **environ;

int access(const char *path, int mode);
int dup(int fd);
int chmod(const char *path, mode_t mode);
int ftruncate(int fd, off_t length);
ssize_t readlink(const char *restrict path, char *restrict buf, size_t bufsiz);
int symlink(const char *path1, const char *path2);
char *realpath(const char *restrict path, char *restrict resolved);
int execvp(const char *file, char *const argv[]);
int execlp(const char *file, const char *arg, ...);

/* Also in <stdlib.h>. */
char *mkdtemp(char *tmpl);
int mkstemp(char *tmpl);
#endif
