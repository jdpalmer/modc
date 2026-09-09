#include "void_union_hdr.h"

int void_union_hdr_run(void) {
	if (!hdr_void_arith((void*)0)) {
		return 1;
	}
	if (hdr_union_ok() != 1) {
		return 2;
	}
	return 0;
}
