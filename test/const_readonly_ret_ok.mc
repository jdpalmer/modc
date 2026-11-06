#include "const_hdr.h"

int const_readonly_ret_ok() {
	const char *p = hdr_msg();
	return *p == 'h' ? 0 : 1;
}
