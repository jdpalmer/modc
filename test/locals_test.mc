/* Portable test (was locals.mc + locals_main.c). */
#include <assert.h>

/* Locals, assignment, and calls */

int add(int a, int b) {
	return a + b;
}

int use_local(int a, int b) {
	int x = { 0 };
	x = a + b;
	return x;
}

int call_add(int a, int b) {
	int x = { 0 };
	x = add(a, b);
	return x;
}

int ptr_store(int* p, int v) {
	*p = v;
	return*p;
}

int main() {
	int x = { 0 };
	assert_eq(use_local(20, 22), 42);
	assert_eq(call_add(10, 32), 42);
	x = 0;
	assert_eq(ptr_store(&x, 7), 7);
	assert_eq(x, 7);
	return 0;
}
