// fail: F:register is not used in %C (allowed in headers)

int bad() {
	register int x = { 0 };
	x = 1;
	return x;
}
