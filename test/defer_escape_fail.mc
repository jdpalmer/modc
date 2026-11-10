// fail: F:invalidated by a defer in this block and cannot escape

#include <stdlib.h>

char* bad_free() {
	char* p = malloc(16);
	defer free(p);
	return p;
}

char* bad_taint() {
	char* p = malloc(16);
	defer free(p);
	char* q = p;
	return q;
}

void take([[releases]] char* p) {
	free(p);
}

char* bad_custom() {
	char* p = malloc(16);
	defer take(p);
	return p;
}

int main() {
	return 0;
}
