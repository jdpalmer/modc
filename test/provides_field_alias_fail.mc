// fail: F:use of released value

#include <stdlib.h>

typedef struct {
	char* p;
} Box;

int main() {
	Box b = { 0 };
	char* z = malloc(16);

	b.p = z;
	free(z);
	*b.p = 'x';
	return 0;
}
