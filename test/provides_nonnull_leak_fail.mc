// fail: F:owned value 'p' never released or retained
#include <stddef.h>

int main() {
	int* p = alloc(int);

	if (p != NULL) {
		(void)p;
	}
	return 0;
}
