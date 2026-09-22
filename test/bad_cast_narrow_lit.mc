int bad(char c) {
	if (c < (char)0x80) {
		return 1;
	}
	if ((c & (char)0xE0) == (char)0xC0) {
		return 2;
	}
	return 0;
}
