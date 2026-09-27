// fail: F:ranged() length exceeds capacity

int f() {
	int a[4] = { 0 };
	return (int)ranged(a, 2, 1).len;
}
