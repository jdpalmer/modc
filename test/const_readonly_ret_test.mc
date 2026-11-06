/* Portable test (was const_readonly_ret.mc + const_readonly_ret_main.c). */
#include <assert.h>

#include "const_hdr.h"

int const_readonly_ret_ok() {
	const char *p = hdr_msg();
	const char[..] s = ranged(p, 1);

	return s[0] == 'h' ? 0 : 1;
}

int main() {
	return const_readonly_ret_ok();
}
