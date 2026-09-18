#include <stdio.h>

int main() {
	char buf[8] = { 0 };
	char[..] v = { 0 };
	int w = 10;

	buf[0] = 'a';
	v = ranged(buf, 1);
	printf("%*s", w, v);
	return 0;
}
