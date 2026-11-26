// fail: F:left shift of a negative value

int bad() {
	return -1 << 1;
}
