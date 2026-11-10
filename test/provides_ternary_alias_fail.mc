// fail: F:use of released value

#include <stdlib.h>

int main() {
	char* a = malloc(16);
	char* b = malloc(16);
	char* q = (a != NULL) ? a : b;

	free(a);
	*q = 'x';
	(void)b;
	return 0;
}
