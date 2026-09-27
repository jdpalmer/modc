// fail: F:comparison between signed and unsigned integers

int bad(int i, unsigned u) {
	return i < u;
}
