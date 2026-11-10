// ok: if (p != NULL) { retain/free } clears the null path at merge
#include <stdlib.h>

int* slot;

void keep([[retains]] int* p) {
	slot = p;
}

int retain_path() {
	int* p = alloc(int);

	if (p != NULL) {
		keep(p);
	}
	return 0;
}

int free_path() {
	int* p = alloc(int);

	if (p != NULL) {
		free(p);
	}
	return 1;
}

int ptr_path() {
	int[..] data = alloc(int, 4);

	if (ptr(data) != NULL) {
		release(&data);
	}
	return 1;
}

int main() {
	return retain_path() + free_path() + ptr_path();
}
