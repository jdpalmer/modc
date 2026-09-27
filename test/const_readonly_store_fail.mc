// fail: F:left operand of assignment is not a modifiable lvalue

#include "const_hdr.h"

int main() {
	const char *p = { 0 };
	p = hdr_msg();
	p[0] = 'X';
	return 0;
}
