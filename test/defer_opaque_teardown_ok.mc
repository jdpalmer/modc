#include <stdio.h>

void flush_file(FILE* f) {
	if (f != NULL) {
		fflush(f);
	}
}

int main() {
	FILE* f = fopen("/dev/null", "w");

	if (f == NULL) {
		return 1;
	}
	/* Helper calls before a recognized release in the same defer are fine. */
	defer {
		flush_file(f);
		fclose(f);
	}
	return 0;
}
