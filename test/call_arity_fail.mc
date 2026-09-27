// fail: F:too many arguments to function call

int bad() {
	return f(1);
}

int f() {
	return 0;
}
