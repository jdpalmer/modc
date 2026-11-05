#include <stdlib.h>
#include <stdio.h>

void take([[consumes]] char* p) {
	free(p);
}

char* reassign_after_free() {
	char* p = malloc(16);
	if (p == NULL) {
		return NULL;
	}
	free(p);
	p = malloc(16);
	if (p == NULL) {
		return NULL;
	}
	p[0] = 'a';
	return p;
}

char* fail_branch_free(int ok) {
	char* p = malloc(16);
	if (p == NULL) {
		return NULL;
	}
	if (!ok) {
		free(p);
		return NULL;
	}
	p[0] = 'b';
	return p;
}

void defer_use_before_leave() {
	char* p = malloc(16);
	if (p == NULL) {
		return;
	}
	defer free(p);
	p[0] = 'c';
}

int main() {
	char* a = reassign_after_free();
	char* b = fail_branch_free(1);
	char* c = fail_branch_free(0);
	defer_use_before_leave();
	free(a);
	free(b);
	if (c != NULL) {
		return 1;
	}
	take(malloc(8));
	printf("ok\n");
	return 0;
}
