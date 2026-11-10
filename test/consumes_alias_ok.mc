// ok:

#include <stdlib.h>

int main() {
	char* p = malloc(16);
	char* q = p;
	char* r = malloc(16);

	/* Rebind leaves the old alias group. */
	p = malloc(16);
	free(p);
	if (q != NULL) {
		*q = 'a';
		free(q);
	}

	p = malloc(16);
	q = p;
	q = r;
	free(p);
	if (q != NULL) {
		*q = 'b';
		free(q);
	}
	return 0;
}
