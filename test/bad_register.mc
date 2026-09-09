int bad(void) {
	register int x = {0};
	x = 1;
	return x;
}
