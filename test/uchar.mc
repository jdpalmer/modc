/* Plain char is unsigned 8-bit. */

int hi_byte_eq(void) {
	char c = {0};
	c = (char)0xFF;
	return c == 0xFF;
}

int hi_byte_as_int(void) {
	char c = {0};
	c = (char)0xFF;
	return (int)c;
}

int uchar_alias(void) {
	unsigned char u = {0};
	char c = {0};
	u = (unsigned char)0xAB;
	c = u;
	return c == 0xAB && u == c;
}
