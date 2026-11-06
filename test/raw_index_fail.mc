// fail: F:cannot index a raw pointer
// fail: F:cannot form *(p + i)

#include <stddef.h>

int main() {
	int* p = NULL;
	p[0] = 1;
	*(p + 1) = 2;
	return 0;
}
