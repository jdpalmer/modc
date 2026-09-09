/* (void) silences discarded multi-return. */

(int, bool) maybe(int x) {
	return (x, true);
}

int ok(void) {
	auto (v, b) = maybe(1);
	(void)maybe(2);
	(void)b;
	return v;
}
