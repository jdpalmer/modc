// fail: F:%C does not allow nested ternary operators; use if/else

int f(int a, int b, int c, int d, int e) {
	return a ? b: c ? d: e;
}
