/* Auto-inline: static small helper expanded at call sites. */
static int add(int a, int b) {
	return a + b;
}

int autoinline_run() {
	int x = { 0 };
	x = add(2, 3);
	if (x != 5) {
		return 1;
	}
	x = add(x, 10);
	if (x != 15) {
		return 2;
	}
	return 0;
}
