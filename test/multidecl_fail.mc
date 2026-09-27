// fail: F:%C requires one variable declaration per line

int f() {
	int a, b;
	a = 1;
	b = 2;
	return a + b;
}
