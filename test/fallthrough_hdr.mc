#include "fallthrough.h"

int from_hdr(void) {
	return hdr_fall(1) == 3 && hdr_fall(2) == 2;
}
