#include "sizeof_param_hdr.h"

int from_hdr() {
	int x = { 0 };
	x = 0;
	return hdr_sizeof_param(&x) == 8;
}
