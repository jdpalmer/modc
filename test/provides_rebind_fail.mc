// fail: F:owned value 'z' overwritten without release
#include <stdlib.h>

int main() {
	int* z = alloc(int);

	z = alloc(int);
	free(z);
	return 0;
}
