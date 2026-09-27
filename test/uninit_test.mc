/* Portable test (was uninit.mc + uninit_main.c). */
#include <assert.h>

/* Locals must be initialized at declaration. */

int init_decl() {
	int x = 3;
	return x;
}

int init_assign() {
	int x = 4;
	return x;
}

int both_branches(int c) {
	int x = c ? 1: 2;
	return x;
}

int do_assigns() {
	int x = 3;
	int n = 3;
	do {
		x = n;
		n--;
	}
	while (n > 0);
	return x;
}

int addr_escape() {
	int x = 0;
	int* p = &x;
	*p = 9;
	return x;
}

int main() {
	assert_eq(init_decl(), 3);
	assert_eq(init_assign(), 4);
	assert_eq(both_branches(1), 1 || both_branches(0) != 2);
	assert_eq(do_assigns(), 1);
	assert_eq(addr_escape(), 9);
	return 0;
}
