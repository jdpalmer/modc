// fail: F:ranged() with one argument requires a fixed array or string literal

int f() {
	int n = { 0 };
	n = 1;
	return ranged(n).len;
}
