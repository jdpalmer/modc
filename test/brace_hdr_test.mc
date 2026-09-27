/* Portable test (was brace_hdr.mc + brace_hdr_main.c). */
#include <assert.h>

#include "brace_hdr.h"

int from_hdr() {
	return hdr_brace(1) == 1 && hdr_brace(0) == 0;
}

int main() {
	assert(from_hdr());

	return 0;
}
