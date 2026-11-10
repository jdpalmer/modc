// fail: F:use of released value

#include <stdlib.h>

void take([[releases]] char* p) {
	free(p);
}

void bad_copy_init() {
	char* p = malloc(16);
	char* q = p;
	free(p);
	*q = 'x';
}

void bad_copy_assign() {
	char* p = malloc(16);
	char* q = malloc(16);
	free(q);
	q = p;
	free(p);
	*q = 'y';
}

void bad_transitive() {
	char* p = malloc(16);
	char* q = p;
	char* r = q;
	free(p);
	*r = 'z';
}

void bad_take_alias() {
	char* p = malloc(16);
	char* q = p;
	take(p);
	*q = 'w';
}

void bad_double_via_alias() {
	char* p = malloc(16);
	char* q = p;
	free(p);
	free(q);
}

int main() {
	return 0;
}
