// fail: F:unnecessary cast from int to long long; remove the cast

#include <stdint.h>

int64_t bad(int n) {
	return (int64_t)n;
}
