#include "long_hdr.h"

int long_hdr_ok(void) {
	if (header_long_size() != 8 && header_long_size() != 4) {
		return 1;
	}
	if (header_long_add(20, 22) != 42) {
		return 2;
	}
	return 0;
}
