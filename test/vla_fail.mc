// fail: F:array size must be a positive constant

int f(int n) {
	int a[n] = { 0 };
	return sizeof(a);
}
