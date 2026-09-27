/* Portable test (was multiret.mc + multiret_main.c). */
#include <assert.h>

/* Multi-return: (T1, T2) functions, return (a, b), destructuring. */

(int, int) pair() {
	return (1, 2);
}

(int, bool) maybe(int x) {
	if (x < 0) {
		return (0, false);
	}
	return (x, true);
}

int use_auto() {
	auto (a, b) = pair();
	return a + b;
}

int use_explicit() {
	(int a, int b) = pair();
	return a + b;
}

int use_maybe_ok() {
	auto (v, ok) = maybe(5);
	if (!ok) {
		return -1;
	}
	return v;
}

int use_maybe_fail() {
	auto (v, ok) = maybe(-1);
	if (!ok) {
		return 0;
	}
	return v;
}

int use_blank_ok() {
	auto (v, _) = maybe(7);
	return v;
}

int use_blank_both() {
	auto (_, _) = maybe(1);
	return 0;
}

int use_blank_explicit() {
	(int v, bool _) = maybe(4);
	return v;
}

int main() {
	assert_eq(use_auto(), 3);
	assert_eq(use_explicit(), 3);
	assert_eq(use_maybe_ok(), 5);
	assert_eq(use_maybe_fail(), 0);
	assert_eq(use_blank_ok(), 7);
	assert_eq(use_blank_both(), 0);
	assert_eq(use_blank_explicit(), 4);
	return 0;
}
