#include <stdlib.h>
#include <stdio.h>

void use(char* p) {
	(void)p;
}

char* ok_use_then_defer() {
	char* p = malloc(16);
	use(p);
	defer free(p);
	return NULL;
}

char* ok_no_defer() {
	char* p = malloc(16);
	return p;
}

void ok_fclose() {
	FILE* f = fopen("/dev/null", "w");
	if (f == NULL) {
		return;
	}
	defer fclose(f);
	fprintf(f, "x");
}

int main() {
	(void)ok_use_then_defer();
	(void)ok_no_defer();
	ok_fclose();
	return 0;
}
