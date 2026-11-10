// fail: F:use of released value

#include <stdlib.h>
#include <stdio.h>

struct Cur {
	char* p;
};

void take([[releases]] char* p) {
	free(p);
}

void bad_deref() {
	char* p = malloc(16);
	free(p);
	*p = 'x';
}

void bad_double_free() {
	char* p = malloc(16);
	free(p);
	free(p);
}

void bad_index() {
	char* p = malloc(16);
	free(p);
	*p = 'x';
}

void bad_custom() {
	char* p = malloc(16);
	take(p);
	*p = 'y';
}

void bad_field() {
	Cur* c = malloc(sizeof(Cur));
	free(c);
	c.p = NULL;
}

void bad_cond() {
	char* p = malloc(16);
	free(p);
	if (p != NULL) {
		(void)p;
	}
}

int main() {
	return 0;
}
