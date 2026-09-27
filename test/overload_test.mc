/* Portable test (was overload.mc + overload_main.c). */
#include <assert.h>

#include <stdint.h>

/* overload keyword: compile-time resolution and mangled linker names. */

overload int max(int a, int b) {
	return a > b ? a: b;
}

overload int64_t max(int64_t a, int64_t b) {
	return a > b ? a: b;
}

overload float max(float a, float b) {
	return a > b ? a: b;
}

int test_int() {
	return max(1, 2);
}

int64_t test_long() {
	return max(1L, 2L);
}

int64_t test_cast_select(int n) {
	return max((int64_t)n, (int64_t)n);
}

float test_float() {
	return max(1.0f, 2.0f);
}

int main() {
	assert_eq(test_int(), 2);
	assert_eq(test_long(), 2);
	assert_eq(test_float(), 2.0f);
	return 0;
}
