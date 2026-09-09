/* Discarded multi-return in for-init / for-step is an error. */

(int, bool) maybe(int x) {
	return (x, true);
}

void bad(void) {
	for (maybe(1); 0; ) {
	}
}
