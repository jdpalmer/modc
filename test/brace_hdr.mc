#include "brace_hdr.h"

int from_hdr() {
	return hdr_brace(1) == 1 && hdr_brace(0) == 0;
}
