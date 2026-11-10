// fail: F:owned value 'buf' never released or retained
#include <stddef.h>

[[provides]] char* bad(int n) {
	char[..] buf = alloc(char, n);

	/* ptr not returned — still owes release */
	(void)ptr(buf);
	return NULL;
}
