/* Portable test (was discard_tuple_ok.mc + discard_tuple_ok_main.c). */
#include <assert.h>

/* (void) silences discarded multi-return. */

(int, bool) maybe(int x) {
	return (x, true);
}

int ok() {
	auto (v, b) = maybe(1);
	(void)maybe(2);
	(void)b;
	return v;
}

int main() {
 assert_eq(ok(), 1);
	return 0; 
}
