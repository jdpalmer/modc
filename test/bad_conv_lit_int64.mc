#include <stdint.h>

int bad() {
	int n = { 0 };
	n = 3000000000;
	return n;
}
