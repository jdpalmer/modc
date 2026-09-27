// fail: F:implicit conversion from int to char requires a cast

int bad() {
	char x = { 0 };
	x = 'a' + 1;
	return x;
}
