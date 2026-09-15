int bad(void) {
	char x = { 0 };
	x = 'a' + 1;
	return (int)x;
}
