#include "autoconst_hdr.h"

int autoconst_readonly_ret_ok(void) {
	char* p = {0};
	p = hdr_msg();
	return p[0] == (char)'h' ? 0: 1;
}
