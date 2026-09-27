/* Portable test (was enum_hdr.mc + enum_hdr_main.c). */
#include <assert.h>

#include "enum_hdr.h"

int from_hdr() {
	return hdr_from_int(1) == 1;
}

int main() {
	assert(from_hdr());

	return 0;
}
