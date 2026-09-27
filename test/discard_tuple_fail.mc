// fail: F:discarded multi-return value; assign it or cast to void

/* Discarded multi-return call is an error. */

(int, bool) maybe(int x) {
	return (x, true);
}

void bad() {
	maybe(1);
}
