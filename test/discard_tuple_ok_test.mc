/* Portable test (was discard_tuple_ok.mc + discard_tuple_ok_main.c). */
#include <assert.h>

/* Bare multi-return discard is allowed without [[nodiscard]]. */

(int, bool) maybe(int x) {
	return (x, true);
}

int ok() {
	auto (v, b) = maybe(1);
	maybe(2);
	(void)maybe(3);
	(void)b;
	return v;
}

int main() {
	assert_eq(ok(), 1);
	return 0;
}
