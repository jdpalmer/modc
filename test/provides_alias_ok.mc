// ok:
#include <stdlib.h>

int main() {
	int* p = alloc(int);
	int* q = p;

	free(q);
	return 0;
}
