// fail: F:putenv' retains its argument

#include <stdlib.h>

int main() {
	char* z = malloc(8);

	putenv(z);
	return 0;
}
