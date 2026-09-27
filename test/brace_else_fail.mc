// fail: F:%C requires braces around else body

int bad_else(int x) {
	if (x) {
		return 1;
	} else return 0;
}
