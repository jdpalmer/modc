/* Portable test (was shift.mc + shift_main.c). */
#include <assert.h>

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

int main() {
	assert_eq(shl_ok(1), 8);
	assert_eq(shr_ok(16), 4);
	assert_eq(shl_long(1), ((int64_t)1 << 40));
	assert_eq(shl_assign(1), 16);
	assert_eq(shl_var(1, 3), 8);
	/* 32 & 31 == 0 → shift by 0 */
	assert_eq(shl_var_wrap(7), 7);
	assert_eq(shl_var(1, 32), 1);
	return 0;
}
