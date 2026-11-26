// fail: F:signed division overflow

int bad() {
	return (-2147483647 - 1) / -1;
}
