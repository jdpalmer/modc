// ok:
#include <stdlib.h>

int main() {
	char* a = malloc(16);
	char* b = a;
	char* q = (a != NULL) ? a : b;

	free(q);
	return 0;
}
