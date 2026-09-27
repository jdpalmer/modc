// fail: F:comparison between signed and unsigned integers

/* Negative signed constants against unsigned still error. */
int bad(unsigned u) {
	return u < -1;
}
