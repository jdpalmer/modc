// fail: F:shift count 32 is out of range for 32-bit type

int bad() {
	int x = { 0 };
	x = 1;
	x <<= 32;
	return x;
}
