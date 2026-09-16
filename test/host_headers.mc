/* Default hosted include tree without extra -I (hermetic stubs). */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

struct Pair {
	int32_t x;
	int32_t y;
};

int host_headers(void) {
	size_t off = { 0 };
	int32_t a = { 0 };
	uint64_t b = { 0 };
	void* p = { 0 };
	bool ok = { 0 };
	char buf[32] = { 0 };
	off = offsetof(struct Pair, y);
	a = 42;
	b = (uint64_t)a + off;
	p = NULL;
	ok = true;
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
	assert(1);
	return 0;
}
