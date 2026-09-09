#include "autoconst_hdr.h"

static void sink(char* p) {
	(void)p;
}

int autoconst_ok(void) {
	char buf[8] = {0};
	char* p = {0};
	if (hdr_strlen("hi") != 2) {
		return 1;
	}
	sink("hi");
	buf[0] = (char)'x';
	buf[1] = (char)0;
	p = buf;
	p[0] = (char)'y';
	(void)hdr_sink;
	return 0;
}
