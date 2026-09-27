// fail: F:implicit conversion from int to short requires a cast

int bad() {
	short x = { 0 };
	x = 70000;
	return x;
}
