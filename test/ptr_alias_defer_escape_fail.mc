// fail: F:invalidated by a defer
#include <stddef.h>

char* bad() {
	char[..] s = alloc(char, 8);

	defer release(&s);
	s[0] = 'a';
	s[1] = 0;
	char* z = ptr(s);
	return z;
}
