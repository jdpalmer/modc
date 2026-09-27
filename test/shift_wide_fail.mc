// fail: F:shift count 32 is out of range for 32-bit type

int bad() {
	return 1 << 32;
}
