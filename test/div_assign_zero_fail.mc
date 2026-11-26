// fail: F:division by zero

int bad() {
	int x = 1;

	x /= 0;
	return x;
}
