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

extern char **environ;
