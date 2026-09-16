/* Constant lo > hi in subrange is an error. */

int bad() {
	int a[4] = { 0 };
	int[..] s = { 0 };
	s = a[3 .. 1];
	return 0;
}
