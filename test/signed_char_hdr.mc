#include "signed_char.h"

int from_hdr() {
	return hdr_signed_char_id(0xFF) == 0xFF;
}
