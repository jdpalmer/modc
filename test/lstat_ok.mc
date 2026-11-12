#include <sys/stat.h>

int lstat_smoke(const char *path, struct stat *st) {
	if (lstat(path, st) != 0) {
		return 0;
	}
	return (st.st_mode & S_IFMT) == S_IFLNK;
}
