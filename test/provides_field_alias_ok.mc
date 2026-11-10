// ok:
#include <stdlib.h>

typedef struct {
	char* p;
} Box;

int main() {
	char* z = malloc(16);
	Box b = { z };

	free(b.p);
	return 0;
}
