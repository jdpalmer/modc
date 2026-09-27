// fail: F:implicit conversion from long long to char requires a cast

#include <stdint.h>

int bad() {
	int64_t y = { 0 };
	char z = { 0 };
	y = 7;
	z = y;
	return z;
}
