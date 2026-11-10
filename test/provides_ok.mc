// ok:
#include <stdlib.h>

[[provides]] int* make() {
	int* z = alloc(int);

	return z;
}

int free_path() {
	int* z = alloc(int);

	free(z);
	return 0;
}

int defer_path() {
	int* z = alloc(int);

	defer free(z);
	return 1;
}

int null_path() {
	int* z = alloc(int);

	if (!z) {
		return 0;
	}
	free(z);
	return 1;
}

int provides_call() {
	int* z = make();

	free(z);
	return 1;
}

int main() {
	return free_path() + defer_path() + null_path() + provides_call();
}
