int bad(void) {
	int x = {0};
	x = 1;
	x <<= 32;
	return x;
}
