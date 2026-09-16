int bad() {
	goto skip;
	int x = 1;
	skip: return x;
}
