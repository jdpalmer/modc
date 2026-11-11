#include <stdlib.h>

void drop_buf([[releases]] char* p) {
	free(p);
}

void log_msg(const char* s) {
	(void)s;
}

int main() {
	char* a = malloc(8);
	char* b = malloc(8);

	if (a == NULL || b == NULL) {
		free(a);
		free(b);
		return 1;
	}
	/* Side-effect-only block is fine (no owned non-release call). */
	defer {
		log_msg("bye");
	}
	defer {
		log_msg("drop a");
		free(a);
	}
	defer drop_buf(b);
	return 0;
}
