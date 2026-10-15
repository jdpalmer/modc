// fail: F:discarded [[nodiscard]] return value; assign it or cast to void

[[nodiscard]] int must_use() {
	return 1;
}

int bad() {
	must_use();
	return 0;
}
