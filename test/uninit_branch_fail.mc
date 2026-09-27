// fail: F:uninitialized local 'x'; initialize at declaration

int bad(int c) {
	int x;
	if (c) {
		x = 1;
	}
	return x;
}
