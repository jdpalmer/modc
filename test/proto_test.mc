/* Portable test (was proto.mc + proto_main.c). */
#include <assert.h>

/* Empty () means zero parameters; typed prototypes are still required. */

int empty_parens() {
	return 1;
}

int another_empty() {
	return empty_parens();
}

int one_arg(int x) {
	return x + 1;
}

int call_ok() {
	return one_arg(41);
}

int main() {
	assert_eq(empty_parens(), 1);
	assert_eq(another_empty(), 1);
	assert_eq(call_ok(), 42);
	return 0;
}
