/* %C: fixed 64-bit integers via int64_t (not host long) */

#include <stdint.h>

int64_t add64(int64_t a, int64_t b) {
	return a + b;
}

int int64_size(void) {
	return (int) sizeof (int64_t);
}
