/* Multi-return: (T1, T2) functions, return (a, b), destructuring. */

(int, int) pair(void) {
	return (1, 2);
}

(int, bool) maybe(int x) {
	if (x < 0) {
		return (0, false);
	}
	return (x, true);
}

int use_auto(void) {
	auto (a, b) = pair();
	return a + b;
}

int use_explicit(void) {
	(int a, int b) = pair();
	return a + b;
}

int use_maybe_ok(void) {
	auto (v, ok) = maybe(5);
	if (!ok) {
		return -1;
	}
	return v;
}

int use_maybe_fail(void) {
	auto (v, ok) = maybe(-1);
	if (!ok) {
		return 0;
	}
	return v;
}
