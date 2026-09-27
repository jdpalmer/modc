// fail: F:shift count 64 is out of range for 64-bit type

#include <stdint.h>

int64_t bad() {
	return 1L << 64;
}
