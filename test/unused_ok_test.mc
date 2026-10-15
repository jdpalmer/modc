/* Portable test (was unused_ok.mc + unused_ok_main.c). */
#include <assert.h>

/* (void)x or [[maybe_unused]] silences unused parameter / local. */

int use_param(int x) {
	(void)x;
	return 1;
}

int use_local() {
	int y = { 0 };
	(void)y;
	return 1;
}

int attr_param([[maybe_unused]] int x) {
	return 1;
}

int attr_local() {
	[[maybe_unused]] int y = { 0 };
	return 1;
}

int main() {
	assert_eq(use_param(0), 1);
	assert_eq(use_local(), 1);
	assert_eq(attr_param(0), 1);
	assert_eq(attr_local(), 1);
	return 0;
}
