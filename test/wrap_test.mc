/* Portable test (was wrap.mc + wrap_main.c). */
#include <assert.h>

/* Signed overflow wraps (two's complement). */

int wrap_add_max() {
	int x = { 0 };
	x = 2147483647;
	return x + 1;
}

int wrap_mul() {
	int x = { 0 };
	x = 1073741824;
	/* 2^30 */
	return x * 2;
}

int main() {
	int a = { 0 };
	int m = { 0 };
	a = wrap_add_max();
	m = wrap_mul();
	assert_eq(a, (int)0x80000000);
	assert_eq(m, (int)0x80000000);
	return 0;
}
