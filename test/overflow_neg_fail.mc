// fail: F:signed integer overflow

int bad() {
	return -(-2147483647 - 1);
}
