/* Portable test (was long_hdr.mc + long_hdr_main.c). */
#include <assert.h>

#include "long_hdr.h"

int long_hdr_ok() {
	if (header_long_size() != 8 && header_long_size() != 4) {
		return 1;
	}
	if (header_long_add(20, 22) != 42) {
		return 2;
	}
	return 0;
}

int main() {
	return long_hdr_ok();
}
