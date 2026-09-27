// fail: F:char[..] with %s requires a bare "%s" (no flags, width, or precision); it is rewritten to %.*s

#include <stdio.h>

int main() {
	char buf[8] = { 0 };
	char[..] v = { 0 };

	buf[0] = 'a';
	v = ranged(buf, 1);
	printf("%-10s", v);
	return 0;
}
