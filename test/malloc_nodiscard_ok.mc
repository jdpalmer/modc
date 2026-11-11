#include <stdlib.h>

int main() {
	void* p = malloc(8);
	void* q = calloc(1, 8);

	if (p != NULL) {
		free(p);
	}
	if (q != NULL) {
		free(q);
	}
	return 0;
}
