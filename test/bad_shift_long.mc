#include <stdint.h>

int64_t bad() {
	return 1L << 64;
}
