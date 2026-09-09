int bad(void) {
	goto skip;
	int x = {0};
	x = 1;
	skip: return x;
}
