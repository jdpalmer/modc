// fail: F:goto jumps over declaration of 'x'

int bad() {
	goto skip;
	int x = 1;
	skip: return x;
}
