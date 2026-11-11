/* Default hosted include tree without extra -I (hermetic stubs). */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

struct Pair {
	int32_t x;
	int32_t y;
};

int host_headers() {
	size_t off = offsetof(struct Pair, y);
	int32_t a = 42;
	uint64_t b = (uint64_t)a + off;
	void* p = NULL;
	bool ok = true;
	char buf[32] = { 0 };
	if (p != 0) {
		return 1;
	}
	if (a != 42) {
		return 2;
	}
	if (b == 0) {
		return 3;
	}
	if (off != 4) {
		return 4;
	}
	if (0b101 != 5) {
		return 5;
	}
	if (!ok) {
		return 6;
	}
	if (false) {
		return 7;
	}
	if (strlen("hi") != 2) {
		return 8;
	}
	if (snprintf(buf, sizeof(buf), "%d", 7) != 1) {
		return 9;
	}
	if (buf[0] != '7') {
		return 10;
	}
	if (abs(-3) != 3) {
		return 11;
	}
	errno = 0;
	if (errno != 0) {
		return 12;
	}
	errno = EINVAL;
	if (errno != EINVAL) {
		return 13;
	}
	if (environ == NULL) {
		return 14;
	}
#ifndef _WIN32
	{
		char *(*md)(char *) = mkdtemp;
		int (*ms)(char *) = mkstemp;
		char *(*rp)(const char *, char *) = realpath;
		int (*acc)(const char *, int) = access;
		int (*dp)(int) = dup;
		ssize_t (*rl)(const char *, char *, size_t) = readlink;
		int (*sy)(const char *, const char *) = symlink;
		int (*cm)(const char *, mode_t) = chmod;
		int (*ft)(int, off_t) = ftruncate;
		(void)md;
		(void)ms;
		(void)rp;
		(void)acc;
		(void)dp;
		(void)rl;
		(void)sy;
		(void)cm;
		(void)ft;
		(void)execvp;
		(void)execlp;
		(void)F_OK;
		(void)R_OK;
	}
#endif
	assert(1);
	return 0;
}
