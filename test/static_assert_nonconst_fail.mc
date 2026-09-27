// fail: F:static_assert expression is not a constant

int bad() {
	int x = { 0 };
	x = 1;
	static_assert(x == 1);
	return 0;
}
