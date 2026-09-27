// fail: F:sizeof on array parameter 'a' is the size of a pointer

int bad(int a[]) {
	return sizeof(a);
}
