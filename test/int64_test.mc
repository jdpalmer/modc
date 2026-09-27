/* Portable test (was int64.mc + int64_main.c). */
#include <assert.h>

/* %C: fixed 64-bit integers via int64_t (not host long) */

#include <stdint.h>

int64_t add64(int64_t a, int64_t b) {
	return a + b;
}

int int64_size() {
	return sizeof(int64_t);
}

int main() {
	assert_eq(int64_size(), 8);
	assert_eq(add64(20, 22), 42);
	return 0;
}
