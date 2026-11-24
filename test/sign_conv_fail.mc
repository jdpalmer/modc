// fail: F:implicit conversion from int to unsigned int requires a cast
#include <stdint.h>

int main() {
	int32_t i = 1;
	uint32_t u = i;

	(void)u;
	return 0;
}
