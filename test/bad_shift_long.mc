#include <stdint.h>

int64_t bad(void) {
	return 1L << 64;
}
