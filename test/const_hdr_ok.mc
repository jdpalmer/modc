#include "const_hdr.h"

static void sink(const char *p) {
	(void)p;
}

int const_hdr_ok() {
	char buf[8] = { 0 };
	char *p = { 0 };
	if (hdr_strlen("hi") != 2) {
		return 1;
	}
	sink("hi");
	buf[0] = 'x';
	buf[1] = 0;
	p = buf;
	p[0] = 'y';
	(void)hdr_sink;
	return 0;
}
