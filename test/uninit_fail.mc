// fail: F:uninitialized local 'x'; initialize at declaration

int bad() {
	int x;
	return x;
}
