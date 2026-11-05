/* Portable test (was lit_suffix.mc + lit_suffix_main.c). */
#include <assert.h>

#include <stdint.h>

/* Literal suffixes: s/us/u/l/ul/f; case; no ll. */

int suf_short() {
	short a = 7s;
	return a == 7 ? 0: 1;
}

int suf_ushort() {
	unsigned short a = 9us;
	return a == 9 ? 0: 1;
}

int suf_uint() {
	unsigned int a = 3u;
	return a == 3 ? 0: 1;
}

int suf_long() {
	int64_t a = 5l;
	return a == 5 && sizeof(a) == 8 ? 0: 1;
}

int suf_ulong() {
	uint64_t a = 6ul;
	return a == 6 ? 0: 1;
}

int suf_lu() {
	uint64_t a = 6lu;
	return a == 6 ? 0: 1;
}

int suf_case() {
	unsigned int a = 4U;
	short b = 2S;
	return a == 4 && b == 2 ? 0: 1;
}

int suf_float() {
	float a = 1.5f;
	double b = 2.5;
	return a == 1.5f && b == 2.5 ? 0: 1;
}

int suf_int_f() {
	float a = 12f;
	return a == 12.0f ? 0: 1;
}

int suf_bin_u() {
	unsigned int a = 0b1010u;
	return a == 10 ? 0: 1;
}

int suf_types() {
	short a = 1s;
	unsigned short b = 1us;
	unsigned int c = 1u;
	int64_t d = 1l;
	uint64_t e = 1ul;
	float f = 1.0f;
	double g = 1.0;
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

int main() {
	assert(!(suf_short()));

	assert(!(suf_ushort()));

	assert(!(suf_uint()));

	assert(!(suf_long()));

	assert(!(suf_ulong()));

	assert(!(suf_lu()));

	assert(!(suf_case()));

	assert(!(suf_float()));

	assert(!(suf_int_f()));

	assert(!(suf_bin_u()));

	assert(!(suf_types()));

	return 0;
}
