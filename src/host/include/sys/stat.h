/*
 * POSIX <sys/stat.h> for %C — curated layouts matching host libc ABI.
 * S_IF* use decimal (frontend currently misreads octal macros).
 */
#pragma once

#include <sys/types.h>
#include <time.h>

/* S_IFMT / S_IFDIR / S_IFREG on Darwin and Linux. */
#define S_IFMT 61440
#define S_IFDIR 16384
#define S_IFREG 32768

#ifdef __APPLE__
struct stat {
	dev_t st_dev;
	mode_t st_mode;
	nlink_t st_nlink;
	ino_t st_ino;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	struct timespec st_atimespec;
	struct timespec st_mtimespec;
	struct timespec st_ctimespec;
	struct timespec st_birthtimespec;
	off_t st_size;
	blkcnt_t st_blocks;
	blksize_t st_blksize;
	unsigned int st_flags;
	unsigned int st_gen;
	int st_lspare;
	long long st_qspare[2];
};
#else
/* Linux x86_64 / aarch64 glibc-style. */
struct stat {
	dev_t st_dev;
	ino_t st_ino;
	nlink_t st_nlink;
	mode_t st_mode;
	uid_t st_uid;
	gid_t st_gid;
	int __pad0;
	dev_t st_rdev;
	off_t st_size;
	blksize_t st_blksize;
	blkcnt_t st_blocks;
	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	long long __glibc_reserved[3];
};
#define st_mtime st_mtim.tv_sec
#endif

int stat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);
