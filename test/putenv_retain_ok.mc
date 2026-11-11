#include <stdlib.h>
#include <string.h>

int main() {
	char* z = malloc(8);

	if (z == NULL) {
		return 1;
	}
	memcpy(z, "A=1", 4);
	/* Hermetic putenv is [[retains]] — discharges ownership at the call. */
	putenv(z);
	return 0;
}
