// ok:

#include <stdlib.h>

int main() {
	char* p = malloc(16);
	char* q = p;

	free(q);
	p = malloc(16);
	q = p;
	free(p);
	return 0;
}
