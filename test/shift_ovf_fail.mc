// fail: F:left shift overflow

int bad() {
	return 1 << 31;
}
