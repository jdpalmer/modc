#include <stdint.h>

/* In-range constant shifts; non-constant counts masked to width. */

int shl_ok(int x) {
	return x << 3;
}

int shr_ok(int x) {
	return x >> 2;
}

int64_t shl_long(int64_t x) {
	return x << 40;
}

int shl_assign(int x) {
	x <<= 4;
	return x;
}

int shl_var(int x, int n) {
	/* n may be >= 32; masked to 5 bits so this is defined */
	return x << n;
}

int shl_var_wrap(int x) {
	int n = { 0 };
	n = 32;
	return x << n;
}
