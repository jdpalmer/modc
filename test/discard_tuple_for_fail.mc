// fail: F:discarded multi-return value; assign it or cast to void

/* Discarded multi-return in for-init / for-step is an error. */

(int, bool) maybe(int x) {
	return (x, true);
}

void bad() {
	for (maybe(1); 0;) {
	}
}
