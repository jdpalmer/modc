/* static multi-return is allowed: storage class before (T, U). */
static(int, int) pair() {
	return (1, 2);
}

int static_tuple_run() {
	auto (a, b) = pair();
	if (a + b != 3) {
		return 1;
	}
	return 0;
}
