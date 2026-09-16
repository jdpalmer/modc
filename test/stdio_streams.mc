/* Darwin: stderr/stdout/stdin are dylib symbols; emit must use QBE extern (GOT). */
#include <stdio.h>

int stdio_streams_run() {
	if (stderr == NULL) {
		return 1;
	}
	if (stdout == NULL) {
		return 2;
	}
	if (stdin == NULL) {
		return 3;
	}
	fprintf(stderr, "");
	return 0;
}
