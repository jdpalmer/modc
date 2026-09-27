/* Portable test (was static_tuple.mc + static_tuple_main.c). */
#include <assert.h>

/* static multi-return is allowed: storage class before (T, U). */
static(int, int) pair() {
	return (1, 2);
}

int static_tuple_run() {
	auto (a, b) = pair();
	if (a + b != 3) {
		return 1;
	}
	return 0;
}

int main() {
	return static_tuple_run();
}
