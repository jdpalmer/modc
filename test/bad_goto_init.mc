int bad(void) {
	goto skip;
	int x = 1;
	skip: return x;
}
