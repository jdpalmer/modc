// fail: F:owned value

#include <stdlib.h>

int main() {
	char[..] s = ranged("hi", 2);
	char* z = cstr(s);

	(void)z;
	return 0;
}
