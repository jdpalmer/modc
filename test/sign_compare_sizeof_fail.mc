// fail: F:comparison between signed and unsigned integers

int bad(int i) {
	char buf[4] = { 0 };
	return i < sizeof(buf);
}
