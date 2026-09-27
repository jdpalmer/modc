// fail: F:shift count -1 is out of range for 32-bit type

int bad() {
	return 1 << -1;
}
