// fail: F:too few arguments to function call

int bad() {
	return f();
}

int f(int x) {
	return x;
}
