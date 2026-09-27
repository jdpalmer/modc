// POSIX helpers for fs (package-internal).
#ifndef _WIN32
#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

int fs_sys_stat(char* path, int* kind, int64_t* size, int64_t* mtime) {
	struct stat st = { 0 };
	int64_t sec = 0;
	int64_t nsec = 0;
	unsigned kindbit = 0;
	if (path == 0 || kind == 0 || size == 0 || mtime == 0) {
		return 0;
	}
	if (stat(path, &st) != 0) {
		return 0;
	}
#ifdef __APPLE__
	sec = st.st_mtimespec.tv_sec;
	nsec = st.st_mtimespec.tv_nsec;
#else
	sec = (int64_t)st.st_mtim.tv_sec;
	nsec = (int64_t)st.st_mtim.tv_nsec;
#endif
	if (nsec < 0) {
		nsec = 0;
	}
	*mtime = sec * 1000000000L + nsec;
	*size = 0;
	kindbit = (unsigned)st.st_mode & (unsigned)S_IFMT;
	if (kindbit == (unsigned)S_IFREG) {
		*kind = 1;
		*size = st.st_size;
	} else if (kindbit == (unsigned)S_IFDIR) {
		*kind = 2;
	} else {
		*kind = 3;
	}
	return 1;
}

void* fs_sys_opendir(char* path) {
	if (path == 0) {
		return 0;
	}
	return opendir(path);
}

int fs_sys_readdir(void* dp, char* name, size_t cap, size_t* nlen) {
	struct dirent* ent = 0;
	size_t n = 0;
	if (dp == 0 || name == 0 || nlen == 0 || cap == 0) {
		return -1;
	}
	errno = 0;
	ent = readdir((DIR *)dp);
	if (ent == 0) {
		if (errno != 0) {
			return -1;
		}
		return 0;
	}
	n = strlen(ent.d_name);
	if (n >= cap) {
		return -1;
	}
	memcpy(name, ent.d_name, n);
	name[n] = 0;
	*nlen = n;
	return 1;
}

void fs_sys_closedir(void* dp) {
	if (dp == 0) {
		return;
	}
	closedir((DIR *)dp);
}
#endif
