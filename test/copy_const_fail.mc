// fail: F:cannot copy into a const T[..]

int bad(const int[..] dst, int[..] src) {
	return copy(dst, src);
}
