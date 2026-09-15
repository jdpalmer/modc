int bad(void) {
	int x = { 0 };
	x = 1;
	static_assert(x == 1);
	return 0;
}
