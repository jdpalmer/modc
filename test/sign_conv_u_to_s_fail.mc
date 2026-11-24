// fail: F:implicit conversion from unsigned int to int requires a cast
#include <stdint.h>

int main() {
	uint32_t u = 1;
	int32_t i = u;

	(void)i;
	return 0;
}
