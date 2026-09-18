/*
 * POSIX <sys/types.h> for %C — declaration-only; host libc at link.
 */
#pragma once

#ifdef __APPLE__
typedef int dev_t;
typedef unsigned short mode_t;
typedef unsigned short nlink_t;
typedef unsigned long long ino_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long long off_t;
typedef long long blkcnt_t;
typedef int blksize_t;
typedef int pid_t;
typedef long long ssize_t;
#elif defined(__linux__)
typedef unsigned long long dev_t;
typedef unsigned int mode_t;
typedef unsigned long nlink_t;
typedef unsigned long long ino_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long long off_t;
typedef long long blkcnt_t;
typedef long blksize_t;
typedef int pid_t;
typedef long long ssize_t;
#else
typedef int dev_t;
typedef unsigned int mode_t;
typedef unsigned int nlink_t;
typedef unsigned long long ino_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long long off_t;
typedef long long blkcnt_t;
typedef int blksize_t;
typedef int pid_t;
typedef long long ssize_t;
#endif
