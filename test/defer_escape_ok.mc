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

[[provides]] char* ok_no_defer() {
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
	{
		char* heap = ok_no_defer();

		free(heap);
	}
	ok_fclose();
	return 0;
}
