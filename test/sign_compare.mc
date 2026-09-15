/* Same-signedness relational compares are fine; casts silence mixes.
 * Non-negative signed constants against unsigned are allowed. */

int both_signed(int a, int b) {
	return a < b;
}

int both_unsigned(unsigned a, unsigned b) {
	return a < b;
}

int sizeof_cast(int i) {
	return i < (int) sizeof(char[8]);
}

int eq_mixed_ok(int i, unsigned u) {
	/* equality is not diagnosed */
	return i == u;
}

int lit_vs_unsigned(unsigned r) {
	return r < 0x7F && r <= 127 && 0x10 < r;
}
