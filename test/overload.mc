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

int test_int(void) {
	return max(1, 2);
}

int64_t test_long(void) {
	return max(1L, 2L);
}

float test_float(void) {
	return max(1.0f, 2.0f);
}
