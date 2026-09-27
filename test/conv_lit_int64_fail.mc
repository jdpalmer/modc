// fail: F:implicit conversion from long long to int requires a cast

#include <stdint.h>

int bad() {
	int n = { 0 };
	n = 3000000000;
	return n;
}
