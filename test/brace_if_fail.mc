// fail: F:%C requires braces around if body

int bad_if(int x) {
	if (x) return 1;
	return 0;
}
