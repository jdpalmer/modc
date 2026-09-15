/* Signed overflow wraps (two's complement). */

int wrap_add_max(void) {
	int x = { 0 };
	x = 2147483647;
	return x + 1;
}

int wrap_mul(void) {
	int x = { 0 };
	x = 1073741824;
	/* 2^30 */
	return x * 2;
}
