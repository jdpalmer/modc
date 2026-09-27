/* Portable test (was sign_compare.mc + sign_compare_main.c). */
#include <assert.h>

/* Same-signedness relational compares are fine; casts silence mixes.
 * Non-negative signed constants against unsigned are allowed. */

int both_signed(int a, int b) {
	return a < b;
}

int both_unsigned(unsigned a, unsigned b) {
	return a < b;
}

int sizeof_cast(int i) {
	return i < (int) sizeof(char[8]);
}

int eq_mixed_ok(int i, unsigned u) {
	/* equality is not diagnosed */
	return i == u;
}

int lit_vs_unsigned(unsigned r) {
	return r < 0x7F && r <= 127 && 0x10 < r;
}

int main() {
	assert(both_signed(1, 2));

	assert(both_unsigned(1, 2));

	assert(sizeof_cast(3));

	assert(eq_mixed_ok(-1, ~0u));

	assert(lit_vs_unsigned(0x20));

	assert(!(lit_vs_unsigned(0x80)));

	return 0;
}
