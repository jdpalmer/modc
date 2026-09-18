#include "const_hdr.h"

static int sink(const char *p) {
	if (p == 0) {
		return 0;
	}
	return hdr_strlen(p);
}

static int view_len(const char[..] s) {
	return (int)len(s);
}

int const_ok() {
	const char *p = "hi";
	const char[..] v = "ab";
	char buf[4] = { 0 };
	char[..] w = { 0 };
	char *m = { 0 };

	if (sink("hi") != 2) {
		return 1;
	}
	if (view_len("xy") != 2) {
		return 2;
	}
	if (view_len(v) != 2) {
		return 3;
	}
	w = ranged(buf, 0, 4);
	m = buf;
	m[0] = 'z';
	/* explicit discard */
	m = (char *)p;
	(void)m;
	(void)w;
	return 0;
}
