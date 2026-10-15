/* Bare multi-return discard is allowed without [[nodiscard]]; (void) still ok. */

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
