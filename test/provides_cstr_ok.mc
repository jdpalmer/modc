// ok:
#include <stdlib.h>

int main() {
	char[..] s = ranged("hi", 2);
	char* z = cstr(s);
	char* lit = cstr("hi");

	free(z);
	(void)lit;
	return 0;
}
