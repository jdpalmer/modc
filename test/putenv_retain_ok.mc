#include <stdlib.h>
#include <string.h>

void env_set([[retains]] char* z) {
	putenv(z);
}

int main() {
	char* z = malloc(8);

	if (z == NULL) {
		return 1;
	}
	memcpy(z, "A=1", 4);
	env_set(z);
	return 0;
}
