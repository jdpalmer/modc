/* Portable test (was const_readonly_ret.mc + const_readonly_ret_main.c). */
#include <assert.h>

#include "const_hdr.h"

int const_readonly_ret_ok() {
	const char *p = { 0 };
	p = hdr_msg();
	return p[0] == 'h' ? 0 : 1;
}

int main() {
	return const_readonly_ret_ok();
}
