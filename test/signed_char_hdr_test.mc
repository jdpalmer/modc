/* Portable test (was signed_char_hdr.mc + signed_char_hdr_main.c). */
#include <assert.h>

#include "signed_char.h"

int from_hdr() {
	return hdr_signed_char_id(0xFF) == 0xFF;
}

int main() {
	assert(from_hdr());

	return 0;
}
