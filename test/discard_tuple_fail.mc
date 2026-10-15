// fail: F:discarded [[nodiscard]] return value; assign it or cast to void

/* Discarded [[nodiscard]] multi-return call is an error. */

[[nodiscard]] (int, bool) maybe(int x) {
	return (x, true);
}

void bad() {
	maybe(1);
}
