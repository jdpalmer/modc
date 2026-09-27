/* Portable test (was fallthrough_hdr.mc + fallthrough_hdr_main.c). */
#include <assert.h>

#include "fallthrough.h"

int from_hdr() {
	return hdr_fall(1) == 3 && hdr_fall(2) == 2;
}

int main() {
	assert(from_hdr());

	return 0;
}
