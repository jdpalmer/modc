int bad(void) {
	short x = {0};
	x = 70000;
	return (int)x;
}
