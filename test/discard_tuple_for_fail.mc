// fail: F:discarded [[nodiscard]] return value; assign it or cast to void

/* Discarded [[nodiscard]] multi-return in for-init is an error. */

[[nodiscard]] (int, bool) maybe(int x) {
	return (x, true);
}

void bad() {
	for (maybe(1); 0;) {
	}
}
