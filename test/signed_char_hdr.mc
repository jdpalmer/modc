#include "signed_char.h"

int from_hdr(void) {
	return hdr_signed_char_id((char)0xFF) == (char)0xFF;
}
