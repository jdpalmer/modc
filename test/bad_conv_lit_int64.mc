#include <stdint.h>

int bad(void) {
	int n = { 0 };
	n = 3000000000;
	return n;
}
