/* Portable test (was add.mc + add_main.c). */
#include <assert.h>

/* Phase 0 parse smoke test */
int add(int a, int b) {
	return a + b;
}

int arithmetic(int a, int b) {
	return a * b - 2;
}

int main() {
	assert_eq(add(20, 22), 42);
	assert_eq(arithmetic(6, 7), 40);
	return 0;
}
