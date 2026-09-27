/* Portable test (was fnptr.mc + fnptr_main.c). */
#include <assert.h>

/* Function pointers: address-of and indirect calls */

int add(int a, int b) {
	return a + b;
}

int sub(int a, int b) {
	return a - b;
}

int callfp(int(*fp)(int, int), int a, int b) {
	return fp(a, b);
}

int callstar(int(*fp)(int, int), int a, int b) {
	return (*fp)(a, b);
}

int via_local() {
	int(*fp)(int, int) = { 0 };
	fp = add;
	return fp(20, 22);
}

int via_addr() {
	int(*fp)(int, int) = { 0 };
	fp = &add;
	return (*fp)(10, 32);
}

int choose(int which, int a, int b) {
	int(*fp)(int, int) = { 0 };
	if (which) {
		fp = add;
	} else {
		fp = sub;
	}
	return fp(a, b);
}

int main() {
	assert_eq(callfp(add, 20, 22), 42);
	assert_eq(callstar(add, 2, 3), 5);
	assert_eq(via_local(), 42);
	assert_eq(via_addr(), 42);
	assert_eq(choose(1, 10, 7), 17);
	assert_eq(choose(0, 10, 7), 3);
	return 0;
}
