#include "const_hdr.h"

int const_readonly_ret_ok() {
	const char *p = { 0 };
	p = hdr_msg();
	return p[0] == (char)'h' ? 0 : 1;
}
