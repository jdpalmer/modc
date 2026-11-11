// fail: F:deferred call uses owned

#include <stdlib.h>

void my_free(char* p) {
	free(p);
}

int main() {
	char* p = malloc(8);

	defer {
		my_free(p);
	}
	return 0;
}
