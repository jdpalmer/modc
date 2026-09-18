/*
 * POSIX <dirent.h> for %C — curated; host libc at link.
 */
#pragma once

#include <sys/types.h>

typedef struct DIR DIR;

#ifdef __APPLE__
struct dirent {
	unsigned long long d_ino;
	unsigned long long d_seekoff;
	unsigned short d_reclen;
	unsigned short d_namlen;
	unsigned char d_type;
	char d_name[1024];
};
#else
/* Linux: glibc dirent with d_type before d_name. */
struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[256];
};
#endif

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dp);
int closedir(DIR *dp);
