#include <stdint.h>

/* Literal suffixes: s/us/u/l/ul/f; case; no ll. */

int suf_short(void) {
	short a = { 0 };
	a = 7s;
	return a == 7 ? 0: 1;
}

int suf_ushort(void) {
	unsigned short a = { 0 };
	a = 9us;
	return a == 9 ? 0: 1;
}

int suf_uint(void) {
	unsigned int a = { 0 };
	a = 3u;
	return a == 3 ? 0: 1;
}

int suf_long(void) {
	int64_t a = { 0 };
	a = 5l;
	return a == 5 && sizeof(a) == 8 ? 0: 1;
}

int suf_ulong(void) {
	uint64_t a = { 0 };
	a = 6ul;
	return a == 6 ? 0: 1;
}

int suf_lu(void) {
	uint64_t a = { 0 };
	a = 6lu;
	return a == 6 ? 0: 1;
}

int suf_case(void) {
	unsigned int a = { 0 };
	short b = { 0 };
	a = 4U;
	b = 2S;
	return a == 4 && b == 2 ? 0: 1;
}

int suf_float(void) {
	float a = { 0 };
	double b = { 0 };
	a = 1.5f;
	b = 2.5;
	return a == 1.5f && b == 2.5 ? 0: 1;
}

int suf_int_f(void) {
	float a = { 0 };
	a = 12f;
	return a == 12.0f ? 0: 1;
}

int suf_bin_u(void) {
	unsigned int a = { 0 };
	a = 0b1010u;
	return a == 10 ? 0: 1;
}

int suf_types(void) {
	short a = { 0 };
	unsigned short b = { 0 };
	unsigned int c = { 0 };
	int64_t d = { 0 };
	uint64_t e = { 0 };
	float f = { 0 };
	double g = { 0 };
	a = 1s;
	b = 1us;
	c = 1u;
	d = 1l;
	e = 1ul;
	f = 1.0f;
	g = 1.0;
	if (a != 1 || b != 1 || c != 1 || d != 1 || e != 1) {
		return 1;
	}
	if (f != 1.0f || g != 1.0) {
		return 2;
	}
	/* Unsuffixed large value is int64_t (narrowing to int would fail). */
	d = 3000000000;
	if (d != 3000000000l) {
		return 3;
	}
	return 0;
}
