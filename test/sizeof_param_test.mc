/* Portable test (was sizeof_param.mc + sizeof_param_main.c). */
#include <assert.h>

/* sizeof on a real array or pointer param is fine; array params decay. */

int sizeof_local() {
	int a[4] = { 0 };
	return sizeof(a);
}

int sizeof_ptr(int* p) {
	return sizeof(p);
}

int sizeof_elem(int a[8]) {
	return sizeof(a[0]);
}

int main() {

	int x = { 0 };
	int a[8] = { 0 };

	x = 0;
	assert_eq(sizeof_local(), 16);
	assert_eq(sizeof_ptr(&x), 8);
	assert_eq(sizeof_elem(a), 4);
	return 0;
}
