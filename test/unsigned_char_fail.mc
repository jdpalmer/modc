// fail: F:%C char is already unsigned; use char, not unsigned char

int bad() {
	unsigned char c = { 0 };
	return c;
}
